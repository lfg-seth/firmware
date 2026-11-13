#include "../../src/modules/SnorrSdHealth.h"
#include <cassert>
#include <cstdio>
int main()
{
    uint8_t bytes[snorr::sdWireSize];
    snorr::encodeSdHealth(bytes, 15, 3, 0x9ea13cc0, 42, 7, 100, 1700000000, 86400, 8589934592ULL, 9007199254740993ULL, 3);
    char actual[105];
    for (unsigned i = 0; i < sizeof(bytes); ++i)
        std::snprintf(actual + i * 2, 3, "%02x", bytes[i]);
    assert(std::strcmp(
               actual,
               "534e5344010f0300c03ca19e2a000000070000006400000000f15365805101000000000002000000010000000000200003000000") == 0);
    snorr::encodeSdHealth(bytes, 15, 3, 0x9ea13cc0, 42, 7, 100, 1700000000, 300, 8589934592ULL, 9007199254740993ULL, 3);
    assert(bytes[28] == 0x2c && bytes[29] == 1 && bytes[30] == 0 && bytes[31] == 0);
    for (uint32_t interval : {180U, 300U, 900U, 86400U}) {
        assert(snorr::sdDue(0, 0, 0, 10, false, interval, 0));
        assert(!snorr::sdDue(1, 100000, 400000, 60, true, interval, interval));
        assert(!snorr::sdDue(1, 0, 400000, 60, false, interval, 0));
        assert(!snorr::sdDue(1, 100000, 100000 + interval - 1, 60, false, interval, 0));
        assert(snorr::sdDue(1, 100000, 100000 + interval, 60, false, interval, 0));
        assert(!snorr::sdDue(1, 0, 0, interval - 1, false, interval, 0));
        assert(snorr::sdDue(1, 0, 0, interval, false, interval, 0));
        assert(!snorr::sdDue(1, 100000, 99999, 60, false, interval, 0));
    }
    assert(!snorr::sdDue(1, 0, 0, 300, true, 180, 900));
    assert(snorr::sdDue(1, 0, 0, 900, true, 180, 900));
    assert(!snorr::sdDue(1, 0, 0, 300, true, 900, 180));
    assert(snorr::sdDue(1, 0, 0, 900, true, 900, 180));
}
