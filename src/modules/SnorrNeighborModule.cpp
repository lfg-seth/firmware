#include "SnorrNeighborModule.h"
#ifdef SNORR_SD_GATEWAY
#include "SnorrSdHealth.h"
#include "mesh/SnorrRecorder.h"
#include <Preferences.h>
#include <esp_timer.h>
#endif

#if USERPREFS_SNORR_NEIGHBOR_SIGNALS
#include "MeshService.h"
#include "NodeDB.h"
#include <Throttle.h>
#include <algorithm>
#include <cstring>
#if HAS_WIFI
#include "RTC.h"
#include "mqtt/MQTT.h"
#include <WiFi.h>
#endif

#ifndef USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS
#define USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS 300
#endif
// Keep the cooldown below half the millis() range, including maximum jitter.
static constexpr uint32_t MAX_INTERVAL_SECS = (INT32_MAX - 15000) / 1000;
static_assert(USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS >= 180 &&
                  USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS <= MAX_INTERVAL_SECS,
              "SNORR radio fallback must fit the cooldown timer and be at least 180 seconds");

SnorrNeighborModule::SnorrNeighborModule()
    : SinglePortModule("snorr-neighbors", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("SnorrNeighbors"),
      bootId(static_cast<uint32_t>(random(1, 0x7fffffff)))
{
    isPromiscuous = true;
    setIntervalFromNow(60 * 1000 + random(0, 15 * 1000));
}

bool SnorrNeighborModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return moduleConfig.neighbor_info.enabled && p->channel == channels.getPrimaryIndex() &&
           snorr::NeighborSignals::isDirectReception(p->from, nodeDB->getNodeNum(), p->via_mqtt, getHopsAway(*p), p->rx_rssi,
                                                     p->rx_snr);
}

ProcessMessage SnorrNeighborModule::handleReceived(const meshtastic_MeshPacket &p)
{
    signals.observe(p.from, millis(), p.rx_rssi, p.rx_snr);
    return ProcessMessage::CONTINUE;
}

int32_t SnorrNeighborModule::runOnce()
{
#if HAS_WIFI
    sendGatewayHealth();
#ifdef SNORR_SD_GATEWAY
    sendSdHealth();
#endif
#endif
    signals.expire(millis());
    if (!moduleConfig.neighbor_info.enabled || !signals.hasChanges())
        return 15 * 1000;

    bool overLora = moduleConfig.neighbor_info.transmit_over_lora;
    if (overLora && (channels.isDefaultChannel(channels.getPrimaryIndex()) || !config.lora.tx_enabled ||
                     config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE ||
                     !airTime->isTxAllowedChannelUtil(true) || !airTime->isTxAllowedAirUtil()))
        return 30 * 1000;
    uint32_t radioSecs = moduleConfig.telemetry.device_update_interval;
    if (!radioSecs)
        radioSecs = USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS;
    const uint32_t minimumMs = overLora ? std::clamp(radioSecs, uint32_t(180), MAX_INTERVAL_SECS) * 1000 : 60 * 1000;
    // Shortening or changing modes must not cancel an outstanding cooldown.
    if (hasReported && Throttle::isWithinTimespanMs(lastReportMs, std::max(minimumMs, reportIntervalMs) + reportJitterMs))
        return 15 * 1000;

    auto packet = allocDataPacket();
    packet->decoded.payload.size = signals.encode(packet->decoded.payload.bytes, sizeof(packet->decoded.payload.bytes),
                                                  nodeDB->getNodeNum(), bootId, ++sequence, millis());
    if (!packet->decoded.payload.size) {
        service->releaseToPool(packet);
        return 15 * 1000;
    }
    packet->channel = channels.getPrimaryIndex();
    packet->to = overLora ? NODENUM_BROADCAST : NODENUM_BROADCAST_NO_LORA;
    packet->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    packet->want_ack = false;
    packet->decoded.want_response = false;
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
    lastReportMs = millis();
    hasReported = true;
    reportIntervalMs = minimumMs;
    reportJitterMs = random(0, 15 * 1000);
    return 15 * 1000;
}

#if HAS_WIFI
#ifdef SNORR_SD_GATEWAY
void SnorrNeighborModule::sendSdHealth()
{
    struct Reservation { uint32_t sequence, utc; };
    static Reservation last{};
    static bool loaded = false;
    static uint64_t lastSeconds = 0;
    static bool sentThisBoot = false;
    static int priorLink = -1;
    const bool wifi = WiFi.status() == WL_CONNECTED;
    int link = (wifi ? 1 : 0) | (wifi && mqtt && mqtt->isConnectedDirectly() ? 2 : 0);
    if (link != priorLink) {
        recorder::event(recorder::Link, 0, 0, link);
        priorLink = link;
    }
    const uint64_t now = esp_timer_get_time() / 1000000;
    const uint32_t utc = getValidTime(RTCQualityFromNet);
    Preferences prefs;
    if (!loaded) {
        if (!prefs.begin("snorr-sd", false)) return;
        if (prefs.isKey("daily") && prefs.getBytes("daily", &last, sizeof(last)) != sizeof(last)) return;
        prefs.end();
        loaded = true;
    }
    if (!snorr::sdDue(last.sequence, last.utc, utc, now - lastSeconds, sentThisBoot) ||
        !moduleConfig.mqtt.enabled || !moduleConfig.mqtt.encryption_enabled || !config.network.wifi_enabled ||
        channels.isDefaultChannel(channels.getPrimaryIndex()) || !config.lora.tx_enabled ||
        !airTime->isTxAllowedChannelUtil(true) || !airTime->isTxAllowedAirUtil())
        return;
    auto p = allocDataPacket();
    if (!p) return;
    // Reserve this generation atomically before emission, including unknown-clock boots.
    Reservation next{last.sequence + 1, utc};
    if (!next.sequence || !prefs.begin("snorr-sd", false) || prefs.putBytes("daily", &next, sizeof(next)) != sizeof(next)) {
        service->releaseToPool(p);
        return;
    }
    prefs.end();
    last = next;
    lastSeconds = now;
    sentThisBoot = true;
    auto s = recorder::storage();
    snorr::encodeSdHealth(p->decoded.payload.bytes, s.flags, utc ? getRTCQuality() : 0, nodeDB->getNodeNum(),
                          recorder::bootId(), next.sequence, now, utc, s.used, s.total, s.drops);
    p->decoded.payload.size = snorr::sdWireSize;
    p->channel = channels.getPrimaryIndex();
    p->to = NODENUM_BROADCAST;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = p->decoded.want_response = false;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}
#endif

void SnorrNeighborModule::sendGatewayHealth()
{
    if (!moduleConfig.neighbor_info.enabled || !moduleConfig.neighbor_info.transmit_over_lora ||
        !moduleConfig.mqtt.enabled || !config.network.wifi_enabled || channels.isDefaultChannel(channels.getPrimaryIndex()) ||
        !config.lora.tx_enabled || config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE ||
        !airTime->isTxAllowedChannelUtil(true) || !airTime->isTxAllowedAirUtil())
        return;
    uint32_t seconds = moduleConfig.telemetry.device_update_interval;
    const uint32_t interval = std::clamp(seconds ? seconds : uint32_t(180), uint32_t(180), MAX_INTERVAL_SECS) * 1000;
    if (hasHealthReport && Throttle::isWithinTimespanMs(lastHealthMs, std::max(interval, healthIntervalMs) + healthJitterMs))
        return;
    auto packet = allocDataPacket();
    auto *data = packet->decoded.payload.bytes;
    memcpy(data, "SNGH", 4);
    data[4] = 1;
    const bool wifi = WiFi.status() == WL_CONNECTED;
    data[5] = (wifi ? 1 : 0) | (wifi && mqtt && mqtt->isConnectedDirectly() ? 2 : 0);
    data[6] = data[7] = 0;
    const uint32_t values[] = {nodeDB->getNodeNum(), bootId, ++healthSequence, millis() / 1000,
                               getValidTime(RTCQualityFromNet), interval / 1000};
    for (size_t i = 0; i < 6; ++i)
        for (size_t j = 0; j < 4; ++j)
            data[8 + i * 4 + j] = values[i] >> (j * 8);
    packet->decoded.payload.size = 32;
    packet->channel = channels.getPrimaryIndex();
    packet->to = NODENUM_BROADCAST;
    packet->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    packet->want_ack = false;
    packet->decoded.want_response = false;
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
    lastHealthMs = millis();
    hasHealthReport = true;
    healthIntervalMs = interval;
    healthJitterMs = random(0, 15 * 1000);
}
#endif
#endif
