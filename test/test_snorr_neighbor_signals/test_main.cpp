#include "modules/SnorrNeighborSignals.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>

using snorr::NeighborSignals;

static uint32_t read32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

static int16_t read16(const uint8_t *p)
{
    return static_cast<int16_t>(uint16_t(p[0]) | uint16_t(p[1]) << 8);
}

static void testReceptionFilter()
{
    auto accepts = NeighborSignals::isDirectReception;
    assert(accepts(2, 1, false, 0, -90, 0));
    assert(!accepts(0, 1, false, 0, -90, 3));
    assert(!accepts(1, 1, false, 0, -90, 3));
    assert(!accepts(2, 1, true, 0, -90, 3));
    assert(!accepts(2, 1, false, 1, -90, 3));
    assert(!accepts(2, 1, false, -1, -90, 3));
    assert(!accepts(2, 1, false, 0, 0, 3));
    assert(!accepts(2, 1, false, 0, -201, 3));
    assert(!accepts(2, 1, false, 0, -90, NAN));
    assert(!accepts(2, 1, false, 0, -90, INFINITY));
}

static void testWireValuesAndFreshness()
{
    NeighborSignals signals;
    uint8_t out[NeighborSignals::MAX_REPORT_BYTES]{};
    assert(signals.encode(out, sizeof(out), 1, 2, 3, 1000) == 0);
    signals.observe(0x12345678, 1000, -98, -7.25f);
    signals.observe(0x12345678, 2000, -91, 0);
    assert(signals.hasChanges());
    assert(signals.encode(out, sizeof(out) - 1, 1, 2, 3, 5000) == 0);
    assert(signals.hasChanges());
    assert(signals.encode(out, sizeof(out), 1, 2, 3, 5000) == 40);
    assert(std::memcmp(out, "SNNS", 4) == 0 && out[4] == 1);
    assert(out[5] == 0 && out[6] == 1 && out[7] == 1);
    assert(read32(out + 8) == 1 && read32(out + 12) == 2 && read32(out + 16) == 3);
    assert(read32(out + 20) == 5 && read32(out + 24) == 0x12345678);
    assert(read32(out + 28) == 3 && read16(out + 32) == -91 && read16(out + 34) == 0);
    assert(read32(out + 36) == 2 && !signals.hasChanges());
    signals.observe(0x12345678, 6000, -100, -7.25f);
    signals.encode(out, sizeof(out), 1, 2, 4, 6000);
    assert(read16(out + 34) == -29 && read32(out + 36) == 3);
}

static void testRotationAndEviction()
{
    NeighborSignals signals;
    uint8_t out[NeighborSignals::MAX_REPORT_BYTES]{};
    for (uint32_t node = 1; node <= NeighborSignals::CAPACITY; ++node)
        signals.observe(node, node * 1000, -100, 3);
    // Refresh the first slot; insertion order must not determine eviction.
    signals.observe(1, 99000, -80, 5);
    signals.observe(1000, 100000, -90, 0);
    std::set<uint32_t> seen;
    for (uint32_t sequence = 0; sequence < (NeighborSignals::CAPACITY + 7) / 8; ++sequence) {
        size_t length = signals.encode(out, sizeof(out), 10, 20, sequence, 101000);
        assert(length <= sizeof(out) && length == 24 + out[6] * 16u);
        assert((out[5] & 2) != 0 && out[7] == NeighborSignals::CAPACITY);
        assert(bool(out[5] & 1) == (NeighborSignals::CAPACITY > 8));
        for (size_t row = 0; row < out[6]; ++row)
            seen.insert(read32(out + 24 + row * 16));
    }
    assert(seen.size() == NeighborSignals::CAPACITY);
    assert(seen.count(1000) == 1 && seen.count(2) == 0);
    if (NeighborSignals::CAPACITY > 1)
        assert(seen.count(1) == 1);
}

static void testExpiryAcrossClockRollover()
{
    NeighborSignals signals;
    uint8_t out[NeighborSignals::MAX_REPORT_BYTES]{};
    constexpr uint32_t start = UINT32_MAX - 2000;
    signals.observe(7, start, -90, 2);
    assert(signals.encode(out, sizeof(out), 1, 2, 3, 999) == 40);
    assert(read32(out + 28) == 3);
    assert(signals.encode(out, sizeof(out), 1, 2, 4, start + NeighborSignals::EXPIRY_MS - 1) == 40);
    assert(signals.encode(out, sizeof(out), 1, 2, 5, start + NeighborSignals::EXPIRY_MS) == 0);
    signals.observe(7, start + NeighborSignals::EXPIRY_MS + 1, -90, 2);
    signals.encode(out, sizeof(out), 1, 2, 6, start + NeighborSignals::EXPIRY_MS + 1);
    assert(read32(out + 36) == 1);
}

static void runTests()
{
    testReceptionFilter();
    testWireValuesAndFreshness();
    testRotationAndEviction();
    testExpiryAcrossClockRollover();
}

#ifdef SNORR_STANDALONE_TEST
int main()
{
    runTests();
    std::puts("PASS: direct filtering, signal wire values, rotation/eviction, "
              "monotonic expiry");
}
#else
#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>
void setUp() {}
void tearDown() {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(runTests);
    exit(UNITY_END());
}
void loop() {}
#endif
