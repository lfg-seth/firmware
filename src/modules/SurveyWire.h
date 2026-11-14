#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace survey
{
// Both targets and the v1 host use little-endian integers. Copy before
// accessing packed RF data.
constexpr uint16_t SURVEY_PORT = 310;
constexpr uint32_t QUIET_MS = 900000;
constexpr uint8_t SIG_LEN = 12;
enum Type : uint8_t { Ping = 1, Pong = 2 };
enum PacketClass : uint8_t { Ordinary, Autonomous, SurveyTx, PositionTx };
struct __attribute__((packed)) Frame {
    uint8_t version, type;
    uint32_t session, attempt, target, utc;
    int16_t rssi, snrQuarter;
    uint8_t utilization;
    uint8_t group[SIG_LEN], peer[SIG_LEN];
};
static_assert(sizeof(Frame) == 47, "survey wire format");
enum Op : uint8_t { Hello, State, Configure, Start, Pause, Resume, Stop, Target, Heartbeat, Enroll, Clock, Profile };
struct __attribute__((packed)) Command {
    char magic[4];
    uint32_t boot, serial;
    uint8_t op, count;
    uint32_t args[10];
    uint8_t keys[64];
};
static_assert(sizeof(Command) == 118, "survey USB request");
struct Settings {
    uint32_t interval = 3000, timeout = 4500, maxInterval = 15000, gpsAge = 2000;
    uint32_t heartbeatTimeout = 6000, gpsInterval = 1000, utilTarget = 65, utilHigh = 80;
    uint32_t adaptive = 1;
    bool valid() const
    {
        return interval >= 1000 && interval <= 60000 && timeout >= 1000 && timeout <= 30000 && maxInterval >= interval &&
               maxInterval <= 120000 && gpsAge >= 500 && gpsAge <= 10000 && heartbeatTimeout >= 2000 &&
               heartbeatTimeout <= 30000 && gpsInterval >= 500 && gpsInterval <= 5000 && utilTarget >= 5 && utilHigh <= 95 &&
               utilHigh > utilTarget && adaptive <= 1;
    }
};
inline bool node(uint32_t n)
{
    return n > 1 && n != UINT32_MAX;
}
inline bool recognizable(const Frame &f)
{
    return f.version == 1 && (f.type == Ping || f.type == Pong) && f.session && f.attempt && node(f.target) && f.utc &&
           f.utilization <= 100 && (f.type != Ping || (f.rssi == 0 && f.snrQuarter == 0));
}
inline bool direct(bool physical, bool mqtt, bool broadcast, unsigned start, unsigned left, bool ack, bool response)
{
    return physical && !mqtt && broadcast && !start && !left && !ack && !response;
}
inline bool recent(uint32_t now, uint32_t stamp, uint32_t age)
{
    return uint32_t(now - stamp) <= age;
}
struct QuietWindow {
    bool armed = false;
    uint32_t last = 0;
    void renew(uint32_t now)
    {
        last = now;
        armed = true;
    }
    uint32_t remaining(uint32_t now) const
    {
        uint32_t elapsed = now - last;
        return armed && elapsed < QUIET_MS ? QUIET_MS - elapsed : 0;
    }
    void tick(uint32_t now)
    {
        if (!remaining(now))
            armed = false;
    }
};
inline bool matches(const Frame &f, uint32_t session, uint32_t attempt, uint32_t target, uint32_t from)
{
    return f.type == Pong && f.session == session && f.attempt == attempt && f.target == target && from == target;
}
} // namespace survey
