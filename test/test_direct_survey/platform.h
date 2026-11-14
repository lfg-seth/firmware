#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using std::max;
using std::min;
inline uint32_t ticks = 5000, wireId = 800;
inline uint32_t millis()
{
    return ticks;
}
inline uint32_t generatePacketId()
{
    return ++wireId;
}
enum {
    RTCQualityNTP,
    ERRNO_OK = 0,
    RX_SRC_LOCAL = 1,
    FILE_O_READ = 1,
    FILE_O_WRITE = 2,
    meshtastic_MeshPacket_decoded_tag = 7,
    meshtastic_PortNum_POSITION_APP = 3,
    meshtastic_PortNum_ROUTING_APP = 5,
    meshtastic_PortNum_ADMIN_APP = 6
};
constexpr auto NODENUM_BROADCAST = UINT32_MAX;
#define optstr(X) "test"
#define HW_VENDOR 9
using meshtastic_PortNum = uint16_t;
using meshtastic_Config_LoRaConfig_ModemPreset = int;
constexpr int meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST = 4;
inline uint32_t getValidTime(int)
{
    return 0;
}
struct meshtastic_Position {
    int32_t latitude_i, longitude_i, altitude;
    uint32_t timestamp, fix_quality, sats_in_view;
};
struct meshtastic_MeshPacket {
    unsigned which_payload_variant = 7;
    uint32_t from = 100, to = NODENUM_BROADCAST, id = 0, hop_limit = 0, hop_start = 0, survey_epoch = 0;
    uint8_t survey_class = 0, survey_physical = 0, via_mqtt = 0, want_ack = 0, channel = 0, survey_block = 0;
    int rx_rssi = -71;
    float rx_snr = 2.5;
    struct {
        uint16_t portnum = 0;
        bool want_response = false;
        uint32_t request_id = 0;
        struct {
            size_t size = 0;
            uint8_t bytes[236]{};
        } payload;
    } decoded;
};
enum class DecodeState { DECODE_SUCCESS, DECODE_FAILURE };
inline DecodeState perhapsDecode(meshtastic_MeshPacket *p, bool)
{
    return p->which_payload_variant == 7 ? DecodeState::DECODE_SUCCESS : DecodeState::DECODE_FAILURE;
}
inline struct DB {
    uint32_t getNodeNum() { return 100; }
} db;
inline auto *nodeDB = &db;
inline bool isFromUs(const meshtastic_MeshPacket *p)
{
    return p->from == 100;
}
inline struct {
    struct {
        struct {
            unsigned size = 32;
            uint8_t bytes[32]{3};
        } private_key;
        bool serial_enabled = true;
    } security;
    struct {
        bool tx_enabled = true, override_duty_cycle = false, use_preset = true;
        unsigned region = 1, channel_num = 35, tx_power = 10;
        int modem_preset = 0;
    } lora;
} config;
inline struct {
    unsigned getPrimaryIndex() { return 0; }
} channels;
inline struct QueueState {
    unsigned free = 8, maxlen = 8;
} q;
inline struct FakeRouter {
    bool surveyProfileKnown = true;
    bool exhausted = false;
    meshtastic_MeshPacket packet;
    unsigned sends = 0, cancels = 0;
    bool applySurveyPreset(int p)
    {
        config.lora.modem_preset = p;
        return true;
    }
    QueueState getQueueStatus() { return q; }
    meshtastic_MeshPacket *allocForSending()
    {
        if (exhausted)
            return nullptr;
        packet = {};
        packet.id = generatePacketId();
        return &packet;
    }
    int sendLocal(meshtastic_MeshPacket *, int)
    {
        ++sends;
        return 0;
    }
    void cancelSending(uint32_t, uint32_t) { ++cancels; }
} fakeRouter;
inline auto *router = &fakeRouter;
inline struct Air {
    float util = 10;
    float channelUtilizationPercent() { return util; }
} air;
inline auto *airTime = &air;
// Deterministic stand-ins only for these state-machine tests; two real radio
// toolchains link the crypto implementation.
struct SHA256 {
    uint32_t value = 1;
    void update(const void *buf, size_t n)
    {
        auto *p = (const uint8_t *)buf;
        while (n--)
            value = value * 31 + *p++;
    }
    void resetHMAC(const void *key, size_t n)
    {
        value = 1;
        update(key, n);
    }
    void finalizeHMAC(const void *key, size_t k, void *buf, size_t n)
    {
        update(key, k);
        auto *p = (uint8_t *)buf;
        while (n--) {
            value = value * 1664525 + 1013904223;
            *p++ = value >> 24;
        }
    }
};
struct Curve25519 {
    static bool dh2(uint8_t *p, uint8_t *) { return p[0] != 0; }
};
inline std::map<std::string, std::vector<uint8_t>> storage;
struct TestFile {
    std::vector<uint8_t> *bytes = nullptr;
    explicit operator bool() const { return bytes; }
    size_t size() const { return bytes->size(); }
    size_t read(uint8_t *p, size_t n)
    {
        n = min(n, bytes->size());
        memcpy(p, bytes->data(), n);
        return n;
    }
    size_t write(const uint8_t *p, size_t n)
    {
        bytes->insert(bytes->end(), p, p + n);
        return n;
    }
    size_t write(uint8_t v) { return write(&v, 1); }
    void close() {}
};
inline struct {
    TestFile open(const char *p, int mode)
    {
        return (mode == FILE_O_WRITE || storage.count(p)) ? TestFile{&storage[p]} : TestFile{};
    }
    void remove(const char *p) { storage.erase(p); }
    bool exists(const char *p) { return storage.count(p); }
} FSCom;
