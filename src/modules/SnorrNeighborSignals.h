#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifndef USERPREFS_SNORR_NEIGHBOR_CAPACITY
#define USERPREFS_SNORR_NEIGHBOR_CAPACITY 32
#endif

namespace snorr
{

class NeighborSignals
{
  public:
    static constexpr size_t CAPACITY = USERPREFS_SNORR_NEIGHBOR_CAPACITY;
    static constexpr size_t ROWS_PER_REPORT = 8;
    static constexpr size_t HEADER_BYTES = 24;
    static constexpr size_t ROW_BYTES = 16;
    static constexpr size_t MAX_REPORT_BYTES = HEADER_BYTES + ROWS_PER_REPORT * ROW_BYTES;
    static constexpr uint32_t EXPIRY_MS = 30 * 60 * 1000;
    static_assert(CAPACITY > 0 && CAPACITY <= 255, "Neighbor capacity must fit the wire count");

    struct Entry {
        uint32_t node = 0;
        uint32_t heardMs = 0;
        uint32_t count = 0;
        int16_t rssi = 0;
        int16_t snrQuarterDb = 0;
    };

    static bool isDirectReception(uint32_t sender, uint32_t observer, bool viaMqtt, int hops, int32_t rssi, float snr)
    {
        return sender != 0 && sender != observer && !viaMqtt && hops == 0 && rssi < 0 && rssi >= -200 && std::isfinite(snr) &&
               snr >= -100 && snr <= 100;
    }

    void expire(uint32_t nowMs)
    {
        for (auto &entry : entries) {
            if (entry.node && nowMs - entry.heardMs >= EXPIRY_MS)
                entry = {};
        }
    }

    void observe(uint32_t node, uint32_t nowMs, int16_t rssi, float snr)
    {
        expire(nowMs);
        Entry *selected = nullptr;
        for (auto &entry : entries) {
            if (entry.node == node) {
                selected = &entry;
                break;
            }
            if (!entry.node && !selected)
                selected = &entry;
        }
        if (!selected) {
            selected = &entries[0];
            for (auto &entry : entries) {
                if (nowMs - entry.heardMs > nowMs - selected->heardMs)
                    selected = &entry;
            }
            evicted = true;
        }
        if (selected->node != node)
            *selected = {};
        selected->node = node;
        selected->heardMs = nowMs;
        selected->rssi = rssi;
        selected->snrQuarterDb = static_cast<int16_t>(std::lround(snr * 4));
        if (selected->count != std::numeric_limits<uint32_t>::max())
            ++selected->count;
        dirty = true;
    }

    bool hasChanges() const { return dirty; }

    size_t encode(uint8_t *out, size_t available, uint32_t observer, uint32_t bootId, uint32_t sequence, uint32_t nowMs)
    {
        if (available < MAX_REPORT_BYTES)
            return 0;
        expire(nowMs);
        size_t total = 0;
        for (const auto &entry : entries)
            total += entry.node != 0;
        if (!total) {
            dirty = false;
            return 0;
        }

        out[0] = 'S';
        out[1] = 'N';
        out[2] = 'N';
        out[3] = 'S';
        out[4] = 1;
        out[5] = (total > ROWS_PER_REPORT ? 1 : 0) | (evicted ? 2 : 0);
        out[7] = static_cast<uint8_t>(total);
        put32(out + 8, observer);
        put32(out + 12, bootId);
        put32(out + 16, sequence);
        put32(out + 20, nowMs / 1000);

        size_t rows = 0;
        size_t scanned = 0;
        while (scanned < CAPACITY && rows < ROWS_PER_REPORT) {
            const auto &entry = entries[cursor];
            cursor = (cursor + 1) % CAPACITY;
            ++scanned;
            if (!entry.node)
                continue;
            auto row = out + HEADER_BYTES + rows * ROW_BYTES;
            put32(row, entry.node);
            put32(row + 4, (nowMs - entry.heardMs) / 1000);
            put16(row + 8, static_cast<uint16_t>(entry.rssi));
            put16(row + 10, static_cast<uint16_t>(entry.snrQuarterDb));
            put32(row + 12, entry.count);
            ++rows;
        }
        out[6] = static_cast<uint8_t>(rows);
        // Oversized tables keep rotating until their entries expire.
        dirty = total > rows;
        return HEADER_BYTES + rows * ROW_BYTES;
    }

  private:
    std::array<Entry, CAPACITY> entries{};
    size_t cursor = 0;
    bool evicted = false;
    bool dirty = false;

    static void put16(uint8_t *out, uint16_t value)
    {
        out[0] = value;
        out[1] = value >> 8;
    }

    static void put32(uint8_t *out, uint32_t value)
    {
        for (size_t i = 0; i < 4; ++i)
            out[i] = value >> (8 * i);
    }
};

} // namespace snorr
