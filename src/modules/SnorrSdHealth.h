#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace snorr
{
constexpr size_t sdWireSize = 52;
inline void putLittle(uint8_t *dst, uint64_t value, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        dst[i] = value >> (i * 8);
}

inline void encodeSdHealth(uint8_t *data, uint8_t flags, uint8_t clockSource, uint32_t node, uint32_t boot, uint32_t sequence,
                           uint32_t uptime, uint32_t utc, uint32_t interval, uint64_t used, uint64_t total, uint32_t dropped)
{
    memcpy(data, "SNSD", 4);
    data[4] = 1;
    data[5] = flags;
    data[6] = clockSource;
    data[7] = 0;
    uint32_t fields[] = {node, boot, sequence, uptime, utc, interval};
    for (size_t i = 0; i < 6; ++i)
        putLittle(data + 8 + i * 4, fields[i], 4);
    putLittle(data + 32, used, 8);
    putLittle(data + 40, total, 8);
    putLittle(data + 48, dropped, 4);
}

// Preserve the outstanding cooldown when the saved telemetry interval shrinks.
inline bool sdDue(uint32_t lastSequence, uint32_t lastUtc, uint32_t utc, uint64_t elapsedSeconds, bool sentThisBoot,
                  uint32_t interval, uint32_t lastInterval)
{
    const uint32_t cooldown = std::max(interval, lastInterval);
    return !lastSequence || elapsedSeconds >= cooldown ||
           (!sentThisBoot && lastUtc && utc >= lastUtc && utc - lastUtc >= cooldown);
}
} // namespace snorr
