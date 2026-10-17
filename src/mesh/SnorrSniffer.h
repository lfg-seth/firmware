#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace snorr
{

inline uint32_t snifferCrc(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return ~crc;
}

template <typename Emit>
void emitSnifferReception(Emit emit, uint32_t boot, uint32_t sequence, uint32_t ms, const uint8_t *data, size_t length, int state,
                          int rssi, float snr, uint32_t airtime)
{
    char line[152];
    snprintf(line, sizeof(line), "SNORR_RX v=1 boot=%08lx seq=%lu ms=%lu len=%u state=%d rssi=%d snr=%.2f toa=%lu",
             (unsigned long)boot, (unsigned long)sequence, (unsigned long)ms, (unsigned)length, state, rssi, snr,
             (unsigned long)airtime);
    emit(line);
    for (size_t offset = 0; offset < length; offset += 40) {
        char hex[81];
        const size_t count = length - offset < 40 ? length - offset : 40;
        for (size_t i = 0; i < count; ++i)
            snprintf(hex + 2 * i, 3, "%02x", data[offset + i]);
        snprintf(line, sizeof(line), "SNORR_DATA seq=%lu off=%u hex=%s", (unsigned long)sequence, (unsigned)offset, hex);
        emit(line);
    }
    snprintf(line, sizeof(line), "SNORR_END seq=%lu crc=%08lx", (unsigned long)sequence, (unsigned long)snifferCrc(data, length));
    emit(line);
}

} // namespace snorr
