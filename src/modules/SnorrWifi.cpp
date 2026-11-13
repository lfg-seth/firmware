#ifdef SNORR_SD_GATEWAY
#include "SnorrWifi.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "mesh/SnorrRecorder.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_timer.h>

namespace snorr {
// All callers run on the cooperative device scheduler. Only the in-RAM comparison
// uses the BSSID. The opaque ordinal changes on a new observed association.
WifiSample sampleWifi()
{
    static uint8_t previous[6]{};
    static bool attached = false;
    static uint32_t association = 0;
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        attached = false;
        return {uint8_t(WiFi.status() == WL_CONNECTED ? 1 : 0), 0, 0, 0};
    }
    if (!attached || memcmp(previous, ap.bssid, sizeof(previous))) {
        memcpy(previous, ap.bssid, sizeof(previous));
        attached = true;
        ++association;
        if (!association) ++association;
    }
    if (!ap.primary || ap.primary > 14 || ap.rssi <= -127 || ap.rssi > 0)
        return {1, 0, 0, 0};
    return {2, ap.rssi, ap.primary, association};
}

static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = n >> (i * 8);
}

void recordSurvey(const WifiSample &s)
{
    uint8_t data[8] = {1, s.state, uint8_t(s.rssi), s.channel};
    put32(data + 4, s.association);
    recorder::event(recorder::WifiSurvey, 0, 0, 0, data, sizeof(data));
}

void broadcastWifi(uint32_t intervalSeconds)
{
    static uint32_t sequence = 0;
    if (!config.network.wifi_enabled || !moduleConfig.mqtt.enabled || !moduleConfig.mqtt.encryption_enabled ||
        !config.lora.tx_enabled || config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE ||
        channels.isDefaultChannel(channels.getPrimaryIndex()) || !recorder::bootId())
        return;
    const auto &channel = channels.getByIndex(channels.getPrimaryIndex());
    if (channel.settings.psk.size != 16 && channel.settings.psk.size != 32) return;
    auto *p = router->allocForSending();
    if (!p) return;
    const auto s = sampleWifi();
    uint8_t *d = p->decoded.payload.bytes;
    memcpy(d, "SNWF", 4);
    d[4] = 1; d[5] = s.state; d[6] = uint8_t(s.rssi); d[7] = s.channel;
    if (!++sequence) ++sequence;
    const uint32_t values[] = {nodeDB->getNodeNum(), recorder::bootId(), sequence, uint32_t(esp_timer_get_time()/1000000),
                               getValidTime(RTCQualityFromNet), intervalSeconds, s.association};
    for (unsigned i = 0; i < 7; ++i) put32(d + 8 + 4*i, values[i]);
    p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    p->decoded.payload.size = 36;
    p->channel = channels.getPrimaryIndex();
    p->to = NODENUM_BROADCAST;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = p->decoded.want_response = false;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}
}
#endif
