#pragma once
#include <cstddef>
#include <cstdint>

#define USERPREFS_SNORR_NEIGHBOR_SIGNALS 1
inline uint32_t testNow = 100000;
inline uint32_t millis()
{
    return testNow;
}
inline long testRandomOffset = 0;
inline long random(long minimum, long maximum)
{
    return minimum + (testRandomOffset < maximum - minimum ? testRandomOffset : maximum - minimum - 1);
}
constexpr int meshtastic_PortNum_PRIVATE_APP = 256;
constexpr int meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE = 1;
constexpr int meshtastic_MeshPacket_Priority_BACKGROUND = 10;
constexpr uint32_t NODENUM_BROADCAST = UINT32_MAX;
constexpr uint32_t NODENUM_BROADCAST_NO_LORA = 1;
constexpr int RX_SRC_LOCAL = 0;

struct meshtastic_MeshPacket {
    uint32_t from = 2;
    uint32_t to = 0;
    uint8_t channel = 0;
    bool via_mqtt = false;
    int hops = 0;
    int32_t rx_rssi = -90;
    float rx_snr = 0;
    int priority = 0;
    bool want_ack = true;
    struct {
        bool want_response = true;
        struct {
            uint8_t bytes[233]{};
            size_t size = 0;
        } payload;
    } decoded;
};
inline struct {
    struct {
        bool enabled = true;
        bool transmit_over_lora = true;
    } neighbor_info;
} moduleConfig;
inline struct {
    struct {
        bool tx_enabled = true;
    } lora;
    struct {
        int role = 0;
    } device;
} config;
inline struct {
    bool isDefault = false;
    uint8_t getPrimaryIndex() { return 0; }
    bool isDefaultChannel(uint8_t) { return isDefault; }
} channels;
inline struct TestAirTime {
    bool channelAllowed = true;
    bool airAllowed = true;
    bool isTxAllowedChannelUtil(bool) { return channelAllowed; }
    bool isTxAllowedAirUtil() { return airAllowed; }
} testAirTime;
inline auto airTime = &testAirTime;
