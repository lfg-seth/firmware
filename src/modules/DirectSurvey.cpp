#include "DirectSurvey.h"
#ifdef SNORR_DIRECT_SURVEY
#include "Channels.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "airtime.h"
#include "main.h"
#include <Curve25519.h>
#include <SHA256.h>
#include <stdarg.h>

namespace survey
{
namespace
{
#ifdef SNORR_SURVEY_CLIENT
constexpr bool IS_CLIENT = true;
#else
constexpr bool IS_CLIENT = false;
#endif
constexpr unsigned MAX_PEERS = 8;
struct Peer {
    uint32_t id;
    uint8_t pub[32];
};
struct Enrollment {
    uint32_t version;
    uint8_t group[32];
    Peer peer[MAX_PEERS];
};
Enrollment enrollment{};
uint8_t pairKeys[MAX_PEERS][32]{};
bool ready = false, stream = false, bench = false, running = false, paused = false;
uint32_t boot, lastSerial, seq, dropped, lastBeat, clockUtc, clockMs, nextDue, interval;
uint32_t session, segment, attempt, pendingId, pendingTarget, queuedAt, txAt, txEnd, txBytes, txEstimate;
uint32_t targetIds[MAX_PEERS]{}, targetCount, cursor, lastGps, lastState, quietEpoch;
uint32_t successes, timeouts, deferred, rejected, suppressed;
uint32_t lastPongId, lastPongSession, lastPongAttempt, lastPongTarget, completedAt;
uint8_t remoteUtil;
meshtastic_Config_LoRaConfig_ModemPreset productionPreset;
bool experimental;
QuietWindow quiet;
Settings settings;
Command previous{};
uint8_t previousError;
struct Fix {
    bool known, alt, sats;
    int32_t lat, lon, height;
    uint32_t stamp, epoch;
    uint16_t frac;
    uint8_t quality, satellites;
};
Fix gps{}, atSend{};
struct Event {
    char text[240];
};
Event events[16];
unsigned head, count;

void emit(const char *type, const char *fmt, ...)
{
    if (!stream)
        return;
    const uint32_t n = ++seq;
    if (count == 16) {
        head = (head + 1) % 16;
        --count;
        ++dropped;
    }
    char *s = events[(head + count) % 16].text;
    int used =
        snprintf(s, sizeof(Event), "%lu,%lu,%lu,%s,", (unsigned long)boot, (unsigned long)n, (unsigned long)millis(), type);
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(s + used, sizeof(Event) - used, fmt, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof(Event) - used) {
        ++dropped;
        return;
    }
    ++count;
}

uint32_t utc()
{
    uint32_t trusted = getValidTime(RTCQualityNTP);
    return trusted ? trusted : clockUtc ? clockUtc + (millis() - clockMs) / 1000 : 0;
}
int peerIndex(uint32_t id)
{
    for (unsigned i = 0; i < MAX_PEERS; ++i)
        if (id && enrollment.peer[i].id == id)
            return i;
    return -1;
}
bool derive(unsigned i)
{
    uint8_t priv[32];
    if (config.security.private_key.size != 32)
        return false;
    memcpy(priv, config.security.private_key.bytes, 32);
    memcpy(pairKeys[i], enrollment.peer[i].pub, 32);
    return Curve25519::dh2(pairKeys[i], priv);
}
bool different(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i)
        diff |= a[i] ^ b[i];
    return diff != 0;
}
// Two alternating, read-back-verified records so interrupted enrollment
// preserves the previous file.
struct Record {
    uint32_t generation;
    Enrollment data;
    uint8_t digest[32];
};
void maskGroup(Record &r)
{
    SHA256 hash;
    uint8_t mask[32];
    hash.resetHMAC(config.security.private_key.bytes, 32);
    hash.update("survey-storage-v1", 17);
    hash.update(&r.generation, 4);
    hash.finalizeHMAC(config.security.private_key.bytes, 32, mask, 32);
    for (unsigned i = 0; i < 32; ++i)
        r.data.group[i] ^= mask[i];
}
constexpr const char *files[] = {"/survey-a", "/survey-b"};
uint32_t generation;
void checksum(Record &r, uint8_t out[32])
{
    SHA256 hash;
    hash.resetHMAC(config.security.private_key.bytes, 32);
    hash.update("survey-record-v1", 16);
    hash.update(&r, offsetof(Record, digest));
    hash.finalizeHMAC(config.security.private_key.bytes, 32, out, 32);
}
bool readRecord(unsigned index, Record &r)
{
    auto f = FSCom.open(files[index], FILE_O_READ);
    if (!f || f.size() != sizeof r || f.read((uint8_t *)&r, sizeof r) != sizeof r)
        return false;
    uint8_t check[32];
    checksum(r, check);
    if (r.data.version != 1 || different(check, r.digest, 32))
        return false;
    maskGroup(r);
    return true;
}
bool enroll(const Command &c)
{
    if (!node(c.args[0]) || c.args[0] == nodeDB->getNodeNum() || running || paused)
        return false;
    uint8_t zero[32]{};
    if (!different(zero, c.keys + 32, 32) || (generation && different(enrollment.group, c.keys + 32, 32)))
        return false;
    int i = peerIndex(c.args[0]);
    if (i < 0) {
        for (i = 0; i < int(MAX_PEERS) && enrollment.peer[i].id; ++i) {
        }
    }
    if (i == int(MAX_PEERS) || (!IS_CLIENT && i != 0))
        return false;
    if (generation && enrollment.peer[i].id == c.args[0] && !different(enrollment.peer[i].pub, c.keys, 32))
        return true;
    uint8_t test[32], priv[32];
    if (config.security.private_key.size != 32)
        return false;
    memcpy(priv, config.security.private_key.bytes, 32);
    memcpy(test, c.keys, 32);
    if (!Curve25519::dh2(test, priv))
        return false;
    Record r{};
    r.generation = generation + 1;
    r.data = enrollment;
    r.data.version = 1;
    r.data.peer[i].id = c.args[0];
    memcpy(r.data.peer[i].pub, c.keys, 32);
    memcpy(r.data.group, c.keys + 32, 32);
    maskGroup(r);
    checksum(r, r.digest);
    unsigned slot = r.generation & 1;
    FSCom.remove(files[slot]);
    auto f = FSCom.open(files[slot], FILE_O_WRITE);
    if (!f || f.write((uint8_t *)&r, sizeof r) != sizeof r)
        return false;
    f.close();
    maskGroup(r);
    Record verified{};
    if (!readRecord(slot, verified) || memcmp(&r, &verified, sizeof r))
        return false;
    enrollment = r.data;
    generation = r.generation;
    memcpy(pairKeys[i], test, 32);
    return true;
}
void tag(const uint8_t key[32], const Frame &frame, uint32_t source, uint32_t packet, uint8_t domain, uint8_t out[SIG_LEN])
{
    SHA256 hash;
    hash.resetHMAC(key, 32);
    hash.update("SNORR-SV1", 9);
    hash.update(&domain, 1);
    hash.update(&source, 4);
    hash.update(&packet, 4);
    hash.update(&frame, offsetof(Frame, group));
    hash.finalizeHMAC(key, 32, out, SIG_LEN);
}
bool fresh(const Fix &f, uint32_t now)
{
    return f.known && recent(now, f.stamp, settings.gpsAge);
}
void sendFix(const char *name, const Fix &f, uint32_t now, uint32_t attemptId)
{
    bool ok = fresh(f, now);
    emit(name, "%u,%u,%u,%ld,%ld,%ld,%u,%u,%u,%u,%u,%u,%u", session, attemptId, ok, (long)(ok ? f.lat : 0),
         (long)(ok ? f.lon : 0), (long)(ok ? f.height : 0), ok && f.alt, ok ? f.epoch : 0, ok ? f.frac : 0,
         f.known ? now - f.stamp : UINT32_MAX, ok ? f.quality : 0, ok && f.sats, ok ? f.satellites : 0);
}
void snapshot()
{
    const auto q = router->getQueueStatus();
    emit("state", "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
         running  ? 1
         : paused ? 2
                  : 0,
         bench, session, segment, targetCount ? targetIds[cursor % targetCount] : 0, interval, quiet.remaining(millis()), dropped,
         q.free, q.maxlen, successes, timeouts, deferred, rejected, suppressed, fresh(gps, millis()),
         lastBeat && recent(millis(), lastBeat, settings.heartbeatTimeout), lastSerial);
    emit("profile", "%u,%u,%u", experimental, config.lora.modem_preset, router->surveyProfileKnown);
    emit("settings", "%u,%u,%u,%u,%u,%u,%u,%u,%u", settings.interval, settings.timeout, settings.maxInterval, settings.gpsAge,
         settings.heartbeatTimeout, settings.gpsInterval, settings.utilTarget, settings.utilHigh, settings.adaptive);
    emit("targets", "%u,%u,%u,%u,%u,%u,%u,%u,%u", targetCount, targetIds[0], targetIds[1], targetIds[2], targetIds[3],
         targetIds[4], targetIds[5], targetIds[6], targetIds[7]);
}
void outcome(const char *status, uint32_t now, const meshtastic_MeshPacket *p = nullptr, const Frame *f = nullptr)
{
    emit("result", "%u,%u,%u,%u,%u,%s,%u,%u,%u,%u,%u,%u,%d,%d,%d,%d,%u,%u,%u,%u", session, segment, attempt, pendingTarget, bench,
         status, pendingId, p ? p->id : 0, queuedAt, txAt, txEnd, now, f ? f->rssi : 0, f ? f->snrQuarter : 0, p ? p->rx_rssi : 0,
         p ? int(lroundf(p->rx_snr * 4)) : 0, txBytes, txEstimate, (unsigned)airTime->channelUtilizationPercent(),
         f ? f->utilization : 255);
    if (pendingId && !p)
        router->cancelSending(nodeDB->getNodeNum(), pendingId);
    lastPongSession = session;
    lastPongAttempt = attempt;
    lastPongTarget = pendingTarget;
    lastPongId = p ? p->id : 0;
    completedAt = now;
    pendingId = 0;
}
void halt(const char *reason, bool keep)
{
    running = false;
    paused = keep;
    if (pendingId)
        outcome(!strcmp(reason, "transport_lost") ? "transport_lost" : "cancelled", millis());
    if (reason[0])
        emit("fault", "%s", reason);
    snapshot();
}
uint32_t ingestUtc(uint32_t clock)
{
    return clock >= 1700000000 ? clock : 0;
}
bool canStart()
{
    return IS_CLIENT && router->surveyProfileKnown && generation && targetCount && utc() && config.lora.tx_enabled &&
           config.lora.region && !config.lora.override_duty_cycle && (bench || fresh(gps, millis()));
}
bool sendFrame(Frame &f, int peer, uint32_t &id)
{
    if (!router->surveyProfileKnown || !generation || peer < 0 || (f.type == Ping && !utc()) || !config.lora.tx_enabled ||
        !config.lora.region || config.lora.override_duty_cycle)
        return false;
    auto *p = router->allocForSending();
    if (!p)
        return false;
    p->hop_limit = 0;
    p->want_ack = false;
    p->to = NODENUM_BROADCAST;
    p->channel = channels.getPrimaryIndex();
    p->survey_class = SurveyTx;
    p->decoded.portnum = static_cast<meshtastic_PortNum>(SURVEY_PORT);
    p->decoded.want_response = false;
    id = p->id;
    f.version = 1;
    if (f.type == Ping)
        f.utc = utc();
    f.utilization = uint8_t(airTime->channelUtilizationPercent());
    tag(enrollment.group, f, p->from, p->id, 1, f.group);
    tag(pairKeys[peer], f, p->from, p->id, 2, f.peer);
    memcpy(p->decoded.payload.bytes, &f, sizeof f);
    p->decoded.payload.size = sizeof f;
    return router->sendLocal(p, RX_SRC_LOCAL) == ERRNO_OK;
}
// Lane zero is reserved for verified tester requests to this responder. Other
// directly heard requests are tracked per source, including the tester using another target.
struct Freshness {
    uint32_t source, restartFloor, reserve, newest;
    unsigned slot;
    uint32_t accepted[8];
    unsigned count;
};
Freshness lanes[MAX_PEERS + 1]{};
struct EpochRecord {
    uint32_t source, epoch;
    uint8_t mac[16];
};
void epochMac(const EpochRecord &record, unsigned lane, uint8_t out[16])
{
    SHA256 h;
    h.resetHMAC(config.security.private_key.bytes, 32);
    h.update("survey-epoch-v2", 15);
    h.update(&lane, sizeof lane);
    h.update(&record, offsetof(EpochRecord, mac));
    h.finalizeHMAC(config.security.private_key.bytes, 32, out, 16);
}
void floorPath(char (&path)[16], unsigned lane, unsigned slot)
{
    snprintf(path, sizeof path, "/sv-f%u-%u", lane, slot);
}
void loadFloor()
{
    memset(lanes, 0, sizeof lanes);
    for (unsigned lane = 0; lane <= MAX_PEERS; ++lane) {
        auto &state = lanes[lane];
        for (unsigned slot = 0; slot < 2; ++slot) {
            char path[16];
            floorPath(path, lane, slot);
            auto file = FSCom.open(path, FILE_O_READ);
            EpochRecord r{};
            uint8_t mac[16];
            if (!file || file.read((uint8_t *)&r, sizeof r) != sizeof r)
                continue;
            epochMac(r, lane, mac);
            if (!different(mac, r.mac, 16) && r.epoch > state.restartFloor) {
                state.source = r.source;
                state.restartFloor = state.reserve = r.epoch;
                state.slot = slot;
            }
        }
    }
}
bool acceptEpoch(uint32_t source, uint32_t packet, uint32_t epoch, bool authorizedTarget)
{
    unsigned lane = 0;
    if (!authorizedTarget) {
        // Reserve slot one for the enrolled tester when it selects a different responder.
        if (source == enrollment.peer[0].id)
            lane = 1;
        else {
            for (lane = 2; lane <= MAX_PEERS; ++lane)
                if (!lanes[lane].source || lanes[lane].source == source)
                    break;
            if (lane > MAX_PEERS)
                return false;
        }
    }
    auto &s = lanes[lane];
    if (epoch <= s.restartFloor || epoch < s.newest || epoch > UINT32_MAX - 60)
        return false;
    if (epoch > s.reserve) {
        EpochRecord r{source, (epoch / 60 + 1) * 60 - 1, {}};
        epochMac(r, lane, r.mac);
        unsigned slot = 1 - s.slot;
        char path[16];
        floorPath(path, lane, slot);
        FSCom.remove(path);
        auto file = FSCom.open(path, FILE_O_WRITE);
        if (!file || file.write((uint8_t *)&r, sizeof r) != sizeof r)
            return false;
        file.close();
        auto verify = FSCom.open(path, FILE_O_READ);
        EpochRecord read{};
        if (!verify || verify.read((uint8_t *)&read, sizeof read) != sizeof read || memcmp(&r, &read, sizeof r))
            return false;
        s.reserve = r.epoch;
        s.slot = slot;
        s.source = source;
    }
    if (epoch > s.newest) {
        s.newest = epoch;
        s.count = 0;
    }
    for (unsigned i = 0; i < s.count; ++i)
        if (s.accepted[i] == packet)
            return false;
    if (s.count == 8)
        return false;
    s.accepted[s.count++] = packet;
    return true;
}
} // namespace

void setup()
{
    productionPreset = config.lora.modem_preset;
    boot = generatePacketId();
    if (!boot)
        boot = 1;
    for (unsigned i = 0; i < 2; ++i) {
        Record r{};
        if (readRecord(i, r) && r.generation > generation) {
            generation = r.generation;
            enrollment = r.data;
        }
    }
    for (unsigned i = 0; i < MAX_PEERS; ++i)
        if (enrollment.peer[i].id && !derive(i))
            enrollment.peer[i].id = 0;
    loadFloor();
    if (!IS_CLIENT) {
        if (generation && FSCom.exists("/sv-quiet")) {
            quiet.renew(millis());
            ++quietEpoch;
        }
    }
    ready = true;
}
size_t pop(uint8_t *buf, size_t limit)
{
    if (!count)
        return 0;
    size_t len = strlen(events[head].text);
    if (len + 4 > limit)
        return 0;
    memcpy(buf, "\0SV1", 4);
    memcpy(buf + 4, events[head].text, len);
    head = (head + 1) % 16;
    --count;
    return len + 4;
}
void fix(const meshtastic_Position &p, uint32_t age, uint16_t frac, bool altitude, bool satellites)
{
    if (!p.timestamp || (gps.known && gps.epoch == p.timestamp && gps.frac == frac))
        return;
    gps = {true,           altitude,    satellites, p.latitude_i,           p.longitude_i,          p.altitude,
           millis() - age, p.timestamp, frac,       uint8_t(p.fix_quality), uint8_t(p.sats_in_view)};
}
void classify(meshtastic_MeshPacket &p, bool local)
{
    if (p.which_payload_variant != meshtastic_MeshPacket_decoded_tag || !isFromUs(&p))
        return;
    if (IS_CLIENT && p.decoded.portnum == meshtastic_PortNum_POSITION_APP)
        p.survey_class = PositionTx;
    else if (local && !p.decoded.request_id && p.decoded.portnum != meshtastic_PortNum_ROUTING_APP &&
             p.decoded.portnum != meshtastic_PortNum_ADMIN_APP)
        p.survey_class = Autonomous;
    p.survey_epoch = quietEpoch;
    p.survey_block = quiet.remaining(millis()) != 0 || (IS_CLIENT && running);
}
bool suppress(const meshtastic_MeshPacket &p)
{
    if (p.survey_class == SurveyTx && !router->surveyProfileKnown)
        return true;
    if (p.survey_class == PositionTx ||
        (p.survey_class == Autonomous &&
         (p.survey_block || (IS_CLIENT && running) || quiet.remaining(millis()) || p.survey_epoch != quietEpoch))) {
        ++suppressed;
        return true;
    }
    if (IS_CLIENT && p.survey_class == SurveyTx)
        return !running || p.id != pendingId || !recent(millis(), lastBeat, settings.heartbeatTimeout) ||
               (!bench && !fresh(gps, millis())) || !recent(millis(), queuedAt, settings.timeout);
    return false;
}
bool intercept(meshtastic_MeshPacket &p)
{
    // Decode a copy. Ordinary packets must retain their original ciphertext and
    // channel hash.
    static meshtastic_MeshPacket scratch; // Cooperative router thread, never consumes a packet-pool slot.
    scratch = p;
    bool decoded = perhapsDecode(&scratch, true) == DecodeState::DECODE_SUCCESS;
    if (p.survey_physical && stream)
        emit("rx", "%u,%u,%u,%u,%u,%u,%u,%d,%d", p.from, p.to, p.id, p.hop_start, p.hop_limit, p.want_ack,
             decoded ? (unsigned)scratch.decoded.portnum : 0, p.rx_rssi, int(lroundf(p.rx_snr * 4)));
    if (!decoded || scratch.decoded.portnum != SURVEY_PORT)
        return false;
    receive(scratch);
    return true;
}
void receive(const meshtastic_MeshPacket &p)
{
    if (!ready || !generation ||
        !direct(p.survey_physical, p.via_mqtt, p.to == NODENUM_BROADCAST, p.hop_start, p.hop_limit, p.want_ack,
                p.decoded.want_response) ||
        p.from == nodeDB->getNodeNum() || p.decoded.payload.size != sizeof(Frame) || p.channel != channels.getPrimaryIndex())
        return;
    Frame f;
    memcpy(&f, p.decoded.payload.bytes, sizeof f);
    uint32_t now = millis(), stamp = utc();
    if (!recognizable(f) || (stamp && (f.utc > stamp ? f.utc - stamp : stamp - f.utc) > 7))
        return;
    uint8_t check[SIG_LEN];
    tag(enrollment.group, f, p.from, p.id, 1, check);
    if (different(check, f.group, SIG_LEN))
        return;
    int peer = peerIndex(p.from);
    bool peerVerified = false;
    if (peer >= 0) {
        tag(pairKeys[peer], f, p.from, p.id, 2, check);
        peerVerified = !different(check, f.peer, SIG_LEN);
    }
    if (f.type == Ping) {
        if (!acceptEpoch(p.from, p.id, f.utc, peerVerified && f.target == nodeDB->getNodeNum()))
            return;
        if (!quiet.remaining(now)) {
            ++quietEpoch;
            if (!IS_CLIENT && !FSCom.exists("/sv-quiet")) {
                auto marker = FSCom.open("/sv-quiet", FILE_O_WRITE);
                if (marker) {
                    marker.write((uint8_t)1);
                    marker.close();
                }
            }
        }
        quiet.renew(now);
        emit("quiet", "%u,%u,%u,%u,%d,%d", p.from, p.id, f.target, QUIET_MS, p.rx_rssi, int(lroundf(p.rx_snr * 4)));
        if (IS_CLIENT || f.target != nodeDB->getNodeNum() || peer < 0)
            return;
    } else if (!IS_CLIENT || peer < 0 || f.target != p.from)
        return;
    if (!peerVerified)
        return;
    if (f.type == Ping) {
        f.type = Pong;
        f.rssi = p.rx_rssi;
        f.snrQuarter = int16_t(lroundf(p.rx_snr * 4));
        uint32_t id = 0;
        bool ok = sendFrame(f, peer, id);
        emit("pong", "%u,%u,%u,%u", p.id, id, ok, f.attempt);
    } else if (pendingId && matches(f, session, attempt, pendingTarget, p.from) && recent(now, queuedAt, settings.timeout)) {
        remoteUtil = f.utilization;
        ++successes;
        outcome("success", now, &p, &f);
    } else {
        bool duplicate = matches(f, lastPongSession, lastPongAttempt, lastPongTarget, p.from) && lastPongId != 0;
        emit("late", "%u,%u,%u,%u,%u,%u", f.session, f.attempt, f.target, p.id, duplicate, completedAt);
    }
}
void txStart(const meshtastic_MeshPacket &p, size_t bytes, uint32_t predictedMs)
{
    emit("airtx", "%u,%u,%u,%u,%u,%u,%u,%u,%u", p.from, p.to, p.id, p.hop_start, p.hop_limit, p.want_ack, p.survey_class,
         (unsigned)bytes, predictedMs);
    if (p.survey_class != SurveyTx)
        return;
    if (IS_CLIENT && p.id == pendingId) {
        txAt = millis();
        txBytes = bytes;
        txEstimate = predictedMs;
        sendFix("txfix", gps, txAt, attempt);
    }
    emit("tx", "%u,%u,%u", p.id, unsigned(bytes), predictedMs);
}
void txDone(const meshtastic_MeshPacket &p)
{
    if (p.survey_class != SurveyTx)
        return;
    if (IS_CLIENT && p.id == pendingId)
        txEnd = millis();
    emit("txdone", "%u", p.id);
}
void poll()
{
    if (!ready)
        return;
    const uint32_t now = millis();
    if (!IS_CLIENT && quiet.armed && !quiet.remaining(now))
        FSCom.remove("/sv-quiet");
    quiet.tick(now);
    if (gps.known && !recent(now, gps.stamp, 60000))
        gps.known = false;
    if (running && (!config.security.serial_enabled || !recent(now, lastBeat, settings.heartbeatTimeout)))
        halt("transport_lost", true);
    if (running && !bench && !fresh(gps, now))
        halt("gps_stale", true);
    if (running && pendingId && !recent(now, queuedAt, settings.timeout)) {
        ++timeouts;
        outcome("timeout", now);
    }
    if (IS_CLIENT && stream && !recent(now, lastGps, settings.gpsInterval - 1)) {
        lastGps = now;
        sendFix("gps", gps, now, 0);
    }
    if (stream && !recent(now, lastState, 4999)) {
        lastState = now;
        snapshot();
    }
    if (!running || pendingId || (int32_t)(now - nextDue) < 0)
        return;
    auto q = router->getQueueStatus();
    unsigned util = (unsigned)airTime->channelUtilizationPercent();
    if (settings.adaptive && remoteUtil > util)
        util = remoteUtil;
    if (settings.adaptive) {
        if (util >= settings.utilTarget)
            interval = min(settings.maxInterval, interval + interval / 2);
        else
            interval = max(settings.interval, interval - interval / 12);
    }
    pendingTarget = targetIds[cursor++ % targetCount];
    ++attempt;
    txAt = txEnd = txBytes = txEstimate = 0;
    queuedAt = now;
    nextDue = now + interval;
    atSend = gps;
    sendFix("sendfix", atSend, now, attempt);
    emit("attempt", "%u,%u,%u,%u,%u,%u,%u,%u", session, segment, attempt, pendingTarget, bench, queuedAt, interval, util);
    if (!q.free || (settings.adaptive && util >= settings.utilHigh)) {
        ++deferred;
        outcome("deferred", now);
        remoteUtil = 0;
        return;
    }
    Frame f{};
    f.type = Ping;
    f.session = session;
    f.attempt = attempt;
    f.target = pendingTarget;
    if (!sendFrame(f, peerIndex(pendingTarget), pendingId)) {
        ++rejected;
        outcome("rejected", now);
    }
}
bool command(const uint8_t *buf, size_t len)
{
    if (len < 4 || memcmp(buf, "\0SV1", 4))
        return false;
    if (!ready)
        return true;
    stream = true;
    if (len != sizeof(Command)) {
        emit("fault", "command_length");
        return true;
    }
    Command c;
    memcpy(&c, buf, len);
    uint8_t err = 0;
    if (c.op == Hello) {
        if (!c.boot && !c.serial && running)
            halt("host_reconnected", true);
        emit("hello", "%u,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u", nodeDB->getNodeNum(), optstr(APP_VERSION), IS_CLIENT, HW_VENDOR,
             config.lora.region, config.lora.modem_preset, config.lora.channel_num, (unsigned)config.lora.tx_power,
             config.lora.use_preset, config.lora.tx_enabled, lastSerial, generation != 0);
        snapshot();
    } else if (c.boot != boot || !c.serial)
        err = 1;
    else if (c.serial == lastSerial && !memcmp(&c, &previous, sizeof c))
        err = previousError;
    else if (c.serial <= lastSerial)
        err = 2;
    else {
        lastSerial = c.serial;
        switch (c.op) {
        case State:
            snapshot();
            break;
        case Heartbeat:
            lastBeat = millis();
            break;
        case Clock:
            if (running || paused || !ingestUtc(c.args[0]))
                err = 3;
            else {
                clockMs = millis();
                clockUtc = c.args[0];
            }
            break;
        case Enroll:
            if (!enroll(c))
                err = 4;
            break;
        case Profile:
            if (running || c.count != 1 || c.args[0] > 1 ||
                !router->applySurveyPreset(c.args[0] ? meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST : productionPreset))
                err = 3;
            else {
                experimental = c.args[0];
                ++segment;
                snapshot();
            }
            break;
        case Configure: {
            Settings s{c.args[0], c.args[1], c.args[2], c.args[3], c.args[4], c.args[5], c.args[6], c.args[7], c.args[8]};
            if (!IS_CLIENT || running || c.count != 9 || !s.valid())
                err = 3;
            else {
                settings = s;
                ++segment;
                snapshot();
            }
            break;
        }
        case Target:
            if (!IS_CLIENT || running || !c.count || c.count > MAX_PEERS)
                err = 3;
            else {
                for (unsigned i = 0; i < c.count; ++i) {
                    if (peerIndex(c.args[i]) < 0)
                        err = 4;
                    for (unsigned j = 0; j < i; ++j)
                        if (c.args[i] == c.args[j])
                            err = 3;
                }
                if (!err) {
                    memcpy(targetIds, c.args, c.count * 4);
                    targetCount = c.count;
                    cursor = 0;
                    ++segment;
                    snapshot();
                }
            }
            break;
        case Start:
        case Resume: {
            if (running || (c.op == Start ? paused : !paused) || c.args[0] > 1) {
                err = 3;
                break;
            }
            if (c.op == Start)
                bench = c.args[0];
            if (!canStart() || !lastBeat || !recent(millis(), lastBeat, settings.heartbeatTimeout)) {
                err = 5;
                emit("fault", !bench && !fresh(gps, millis()) ? "gps_stale" : "start_interlock");
                break;
            }
            if (c.op == Start) {
                session = generatePacketId();
                if (!session)
                    session = 1;
                segment = 1;
                attempt = 0;
            } else
                ++segment;
            ++quietEpoch;
            running = true;
            paused = false;
            interval = settings.interval;
            nextDue = millis();
            snapshot();
            break;
        }
        case Pause:
            if (!running)
                err = 3;
            else
                halt("", true);
            break;
        case Stop:
            halt("", false);
            break;
        default:
            err = 6;
        }
        previous = c;
        previousError = err;
    }
    if (c.op == Profile && !router->surveyProfileKnown)
        emit("fault", "radio_profile_unknown");
    emit("ack", "%u,%u,%u", c.serial, c.op, err);
    return true;
}
} // namespace survey
#endif
