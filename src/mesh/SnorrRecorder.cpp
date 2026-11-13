#ifdef SNORR_SD_GATEWAY
#ifdef SNORR_RECORDER_HOST_TEST
#include "recorder-fixture.h"
#else
#include "SnorrRecorder.h"
#include "SnorrSniffer.h"
#include "RTC.h"
#include "NodeDB.h"
#include "modules/SnorrSdHealth.h"
#include <SD.h>
#include <SPI.h>

#include <vfs_api.h>
#include <ff.h>
#include <esp_timer.h>
#endif
#include "modules/SnorrSdHealth.h"
#include "SnorrSniffer.h"
#include <algorithm>
#include <atomic>
#include <cinttypes>

namespace recorder
{
namespace
{
// Own the second SPI controller. RadioLib uses the Arduino global SPI (FSPI).
class Card : public fs::SDFS {
  public:
    Card() : SDFS(fs::FSImplPtr(new VFSImpl)) {}
    char drive() const { return '0' + _pdrv; }
} card;
SPIClass bus(HSPI);
constexpr size_t maxPayload = 640;
constexpr uint32_t segmentLimit = 1024 * 1024;
constexpr uint16_t headerSize = 24;
constexpr uint16_t frameHeader = 48;
struct Entry {
    uint64_t ms;
    uint32_t seq, utc, from, id, related;
    int32_t detail;
    int16_t rssi, snr;
    uint16_t length;
    uint8_t kind, clock;
    uint8_t data[maxPayload];
};
QueueHandle_t queue;
uint32_t source, boot;
std::atomic<uint32_t> sequence{0}, losses{0};
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
Storage snapshot{};
Entry bootEntry{};
char folder[32], path[64];
uint32_t serial, frames, lastFlushedLoss, unsyncedEvents, uncertain;
uint32_t lastRawId, lastOwnId;
FIL file;
bool opened, mounted;
uint32_t clusterSize;
uint64_t used, total;
uint8_t frame[frameHeader + maxPayload + 4];

uint64_t uptime() { return esp_timer_get_time() / 1000; }
uint32_t word(const uint8_t *b)
{
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
using snorr::putLittle;
using snorr::snifferCrc;
void state(uint8_t flags)
{
    portENTER_CRITICAL(&stateLock);
    snapshot.flags = flags;
    snapshot.used = flags & 8 ? used : 0;
    snapshot.total = flags & 8 ? total : 0;
    portEXIT_CRITICAL(&stateLock);
}
bool capacity()
{
    char drive[] = {card.drive(), ':', 0};
    DWORD freeClusters;
    FATFS *fs;
    if (f_getfree(drive, &freeClusters, &fs) != FR_OK || freeClusters > fs->n_fatent - 2)
        return false;
    clusterSize = uint32_t(fs->csize) * card.sectorSize();
    total = uint64_t(fs->n_fatent - 2) * clusterSize;
    used = total - uint64_t(freeClusters) * clusterSize;
    return total && clusterSize;
}
void makePath(char *dest, uint32_t number)
{
    snprintf(dest, 64, "%s/%08" PRIX32 ".SML", folder, number);
}
bool readExact(FIL &f, void *dest, UINT size)
{
    UINT got;
    return f_read(&f, dest, size, &got) == FR_OK && got == size;
}
bool writeExact(const void *data, UINT size)
{
    UINT wrote;
    return f_write(&file, data, size, &wrote) == FR_OK && wrote == size;
}
bool ownedHeader(const uint8_t *h, uint32_t n)
{
    return !memcmp(h, "SML1", 4) && word(h + 4) == source && word(h + 8) == n &&
           word(h + 12) && word(h + 16) == 1 && word(h + 20) == snifferCrc(h, 20);
}
// Before deleting, verify the whole closed segment, including the closing frame count.
// A name collision, truncated tail or damaged body always survives reclamation.
bool closedSegment(const char *name, uint32_t n, bool seal = false)
{
    FIL in;
    if (f_open(&in, name, seal ? FA_READ | FA_WRITE : FA_READ) != FR_OK)
        return false;
    uint8_t buf[frameHeader + maxPayload + 4];
    bool valid = f_size(&in) <= segmentLimit && readExact(in, buf, headerSize) && ownedHeader(buf, n);
    uint32_t ownerBoot = valid ? word(buf + 12) : 0;
    uint32_t count = 0;
    while (valid && !f_eof(&in)) {
        if (!readExact(in, buf, 4)) { valid = false; break; }
        if (!memcmp(buf, "END1", 4)) {
            bool closed = readExact(in, buf + 4, 8) && word(buf + 4) == count &&
                          word(buf + 8) == snifferCrc(buf, 8) && f_eof(&in);
            f_close(&in);
            return closed;
        }
        if (memcmp(buf, "EVT1", 4) || !readExact(in, buf + 4, frameHeader - 4)) { valid = false; break; }
        unsigned len = unsigned(buf[4]) | unsigned(buf[5]) << 8;
        valid = word(buf + 44) == ownerBoot && len <= maxPayload && readExact(in, buf + frameHeader, len + 4) &&
                word(buf + frameHeader + len) == snifferCrc(buf, frameHeader + len);
        ++count;
    }
    if (seal && valid && count && f_eof(&in) && f_size(&in) + 12 <= segmentLimit) {
        uint8_t end[12] = {'E','N','D','1'};
        putLittle(end + 4, count, 4);
        putLittle(end + 8, snifferCrc(end, 8), 4);
        UINT wrote;
        f_write(&in, end, 12, &wrote);
    }
    f_close(&in);
    return false;
}
bool numberOf(const char *name, uint32_t &n)
{
    if (strlen(name) != 12 || strcasecmp(name + 8, ".SML") || strspn(name, "0123456789ABCDEFabcdef") != 8)
        return false;
    n = strtoul(name, nullptr, 16);
    return n != 0;
}
bool scan(bool reclaim, uint32_t &selected)
{
    FF_DIR dir;
    FILINFO info;
    if (f_opendir(&dir, folder) != FR_OK)
        return false;
    FRESULT result;
    selected = 0;
    while ((result = f_readdir(&dir, &info)) == FR_OK && *info.fname) {
        uint32_t n;
        if (info.fattrib & AM_DIR || !numberOf(info.fname, n)) continue;
        if (!reclaim) {
            char candidate[64];
            makePath(candidate, n);
            FIL in;
            if (f_open(&in, candidate, FA_READ) != FR_OK) continue;
            uint8_t h[headerSize];
            bool owned = readExact(in, h, headerSize) && ownedHeader(h, n);
            f_close(&in);
            if (owned) selected = std::max(selected, n);
        } else if ((!opened || n != serial) && (!selected || n < selected)) {
            char candidate[64];
            makePath(candidate, n);
            if (closedSegment(candidate, n)) selected = n;
        }
    }
    f_closedir(&dir);
    return result == FR_OK;
}
uint32_t evicted, evictions;
// Only the writer changes FAT state.
bool makeRoom(unsigned size)
{
    // A partial final cluster is already allocated. Reserve the 12-byte close marker
    // with each record so a full volume can always close the current segment.
    uint32_t offset = opened ? f_size(&file) % clusterSize : 0;
    uint32_t available = offset ? clusterSize - offset : 0;
    if (size <= available) return true;
    if (!capacity()) return false;
    uint64_t required = ((size - available + clusterSize - 1) / clusterSize) * uint64_t(clusterSize);
    if (total - used >= required) return true;
    uint32_t oldest;
    if (!scan(true, oldest) || !oldest) return false;
    char victim[64];
    makePath(victim, oldest);
    if (f_unlink(victim) != FR_OK) return false;
    evicted = oldest;
    ++evictions;
    return capacity() && total - used >= required;
}
bool closeSegment()
{
    uint8_t end[12] = {'E','N','D','1'};
    putLittle(end + 4, frames, 4);
    putLittle(end + 8, snifferCrc(end, 8), 4);
    bool ok = writeExact(end, sizeof(end));
    FRESULT result = f_close(&file);
    opened = false;
    if (ok && result == FR_OK) unsyncedEvents = 0;
    return ok && result == FR_OK;
}
bool newSegment()
{
    if (serial == UINT32_MAX || !makeRoom(headerSize + 12)) return false;
    FRESULT create;
    do {
        if (serial == UINT32_MAX) return false;
        makePath(path, ++serial);
        create = f_open(&file, path, FA_CREATE_NEW | FA_WRITE);
    } while (create == FR_EXIST);
    if (create == FR_DENIED && capacity() && total == used && makeRoom(clusterSize))
        create = f_open(&file, path, FA_CREATE_NEW | FA_WRITE);
    if (create != FR_OK) return false;
    opened = true;
    frames = 0;
    uint8_t h[headerSize] = {'S','M','L','1'};
    putLittle(h + 4, source, 4);
    putLittle(h + 8, serial, 4);
    putLittle(h + 12, boot, 4);
    putLittle(h + 16, 1, 4);
    putLittle(h + 20, snifferCrc(h, 20), 4);
    return writeExact(h, sizeof(h));
}
bool append(const Entry &e)
{
    unsigned size = frameHeader + e.length + 4;
    if (opened && f_size(&file) + size + 12 > segmentLimit && !closeSegment()) return false;
    if (!opened && !newSegment()) return false;
    if (!makeRoom(size + 12)) return false;
    memcpy(frame, "EVT1", 4);
    putLittle(frame + 4, e.length, 2);
    frame[6] = e.kind;
    frame[7] = e.clock;
    putLittle(frame + 8, e.seq, 4);
    putLittle(frame + 12, e.ms, 8);
    putLittle(frame + 20, e.utc, 4);
    putLittle(frame + 24, e.from, 4);
    putLittle(frame + 28, e.id, 4);
    putLittle(frame + 32, e.related, 4);
    putLittle(frame + 36, e.detail, 4);
    putLittle(frame + 40, e.rssi, 2);
    putLittle(frame + 42, e.snr, 2);
    putLittle(frame + 44, boot, 4);
    memcpy(frame + frameHeader, e.data, e.length);
    putLittle(frame + size - 4, snifferCrc(frame, size - 4), 4);
    if (!writeExact(frame, size)) return false;
    ++frames;
    if (e.kind == RawRx) lastRawId = e.id;
    if (e.kind == Local) lastOwnId = e.id;
    ++unsyncedEvents;
    return true;
}
#ifndef SNORR_RECORDER_HOST_TEST
Entry entry(Kind kind)
{
    Entry e{};
    e.kind = kind;
    e.seq = ++sequence;
    e.ms = uptime();
    e.utc = getValidTime(RTCQualityDevice);
    e.clock = e.utc ? getRTCQuality() : 0;
    return e;
}
#endif
void unmount()
{
    if (opened) f_close(&file);
    opened = false;
    card.end();
    mounted = false;
}
bool mount()
{
    if (!card.begin(4, bus, 1000000, "/snorrsd", 5, false)) { state(0); return false; }
    mounted = true;
    state(3);
    snprintf(folder, sizeof(folder), "%c:/SN%08" PRIX32, card.drive(), source);
    FRESULT mk = f_mkdir(folder);
    if ((mk != FR_OK && mk != FR_EXIST) || !capacity() || !scan(false, serial)) return false;
    if (serial) {
        char previous[64];
        makePath(previous, serial);
        closedSegment(previous, serial, true); // Seal only fully recoverable boot tails.
    }
    return append(bootEntry) && f_sync(&file) == FR_OK;
}
#ifndef SNORR_RECORDER_HOST_TEST
void writer(void *)
{
    bus.begin(47, 6, 48, 4);
    uint64_t retry = 0, sync = 0, diagnostic = 0;
    uint8_t priorClock = 255;
    int64_t clockOffset = 0;
    uint32_t reportedEvictions = 0, pendingLoss = 0, reportedUncertain = 0;
    for (;;) {
        uint64_t now = uptime();
        if (!mounted && now >= retry) {
            if (!mount()) {
                uint8_t failedState = mounted ? 1 : 0;
                unmount();
                state(failedState);
                retry = now + 15000;
            }
            else state(15);
        }
        Entry e;
        bool ok = true;
        if (xQueueReceive(queue, &e, pdMS_TO_TICKS(200))) {
            if (!mounted || !(ok = append(e))) ++losses;
        }
        if (!mounted) continue;
        uint32_t lost = losses.load();
        if (ok && (lost != pendingLoss || uncertain != reportedUncertain)) {
            Entry gap = entry(Gap);
            gap.id = lost;
            gap.related = pendingLoss;
            gap.detail = uncertain;
            ok = append(gap);
            if (ok) { pendingLoss = lost; reportedUncertain = uncertain; }
        }
        if (ok && evictions != reportedEvictions) {
            Entry eviction = entry(Eviction);
            eviction.id = evicted;
            eviction.related = evictions;
            ok = append(eviction);
            if (ok) reportedEvictions = eviction.related;
        }
        if (ok && now >= sync) {
            Entry anchor{};
            anchor.ms = uptime();
            anchor.utc = getValidTime(RTCQualityDevice);
            anchor.clock = anchor.utc ? getRTCQuality() : 0;
            int64_t offset = int64_t(anchor.utc) - int64_t(anchor.ms / 1000);
            if (anchor.clock != priorClock || (anchor.clock && llabs(offset - clockOffset) > 2)) {
                anchor.kind = Clock;
                anchor.seq = ++sequence;
                ok = append(anchor);
                priorClock = anchor.clock;
                clockOffset = offset;
            }
            if (ok) ok = f_sync(&file) == FR_OK && capacity();
            if (ok) {
                state(15);
                lastFlushedLoss = pendingLoss;
                unsyncedEvents = 0;
                if (now >= diagnostic) {
                    LOG_INFO("SNSD sync segment=%u frames=%u drops=%u lastRaw=%08x lastOwn=%08x usedKiB=%u totalKiB=%u",
                             serial, frames, lost, lastRawId, lastOwnId, uint32_t(used / 1024), uint32_t(total / 1024));
                    diagnostic = now + 60000;
                }
            }
            sync = now + 5000;
        }
        if (!ok) {
            // The failed sync cannot tell which accepted writes reached the medium.
            uncertain += unsyncedEvents;
            unsyncedEvents = 0;
            state(1);
            pendingLoss = lastFlushedLoss;
            unmount();
            retry = now + 15000;
        }

    }
}
} // namespace

void begin(uint32_t node)
{
    source = node;
    do { boot = esp_random(); } while (!boot);
    queue = xQueueCreate(32, sizeof(Entry));
    bootEntry = entry(Boot);
    bootEntry.detail = esp_reset_reason();
    bootEntry.length = snprintf(reinterpret_cast<char *>(bootEntry.data), maxPayload,
        "fw=%s region=%u preset=%u tx=%u hop=%u role=%u rebroadcast=%u interval=%u", optstr(APP_VERSION),
        config.lora.region, config.lora.modem_preset, config.lora.tx_power, config.lora.hop_limit,
        config.device.role, config.device.rebroadcast_mode, moduleConfig.telemetry.device_update_interval);
    if (queue && xTaskCreatePinnedToCore(writer, "snorr-sd", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
        vQueueDelete(queue); queue = nullptr;
    }
}
uint32_t bootId() { return boot; }
Storage storage()
{
    portENTER_CRITICAL(&stateLock);
    Storage s = snapshot;
    portEXIT_CRITICAL(&stateLock);
    s.drops = losses.load();
    return s;
}
uint32_t event(Kind kind, uint32_t from, uint32_t id, int32_t detail, const void *bytes, size_t size, uint32_t related,
               int16_t rssi, int16_t snrQuarter)
{
    Entry e = entry(kind);
    e.from = from; e.id = id; e.detail = detail; e.related = related;
    e.rssi = rssi; e.snr = snrQuarter;
    if (size <= maxPayload) {
        e.length = size;
        if (size) memcpy(e.data, bytes, size);
        if (queue && xQueueSend(queue, &e, 0) == pdTRUE) return e.seq;
    }
    ++losses;
    return e.seq;
}
void packet(Kind kind, const meshtastic_MeshPacket &p, int32_t detail)
{
    const bool decoded = p.which_payload_variant == meshtastic_MeshPacket_decoded_tag;
    const bool content = decoded && p.decoded.portnum != meshtastic_PortNum_ADMIN_APP;
    event(kind, p.from, p.id, decoded ? int32_t(p.decoded.portnum) : detail,
          content ? p.decoded.payload.bytes : nullptr, content ? p.decoded.payload.size : 0,
          p.snorr_rx_sequence);
}
#else
} // namespace
#endif
} // namespace recorder
#endif
