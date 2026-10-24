#include "SnorrNeighborModule.h"

#if USERPREFS_SNORR_NEIGHBOR_SIGNALS
#include "MeshService.h"
#include "NodeDB.h"
#include <Throttle.h>
#include <algorithm>

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
#endif
