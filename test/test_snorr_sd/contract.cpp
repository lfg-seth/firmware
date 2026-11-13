#include "../../src/modules/SnorrSdHealth.h"
#include <cassert>
#include <cstdio>
int main()
{
    uint8_t bytes[snorr::sdWireSize];
    snorr::encodeSdHealth(bytes, 15, 3, 0x9ea13cc0, 42, 7, 100, 1700000000, 8589934592ULL, 9007199254740993ULL, 3);
    char actual[105];
    for (unsigned i = 0; i < sizeof(bytes); ++i) std::snprintf(actual + i*2, 3, "%02x", bytes[i]);
    assert(std::strcmp(actual, "534e5344010f0300c03ca19e2a000000070000006400000000f15365805101000000000002000000010000000000200003000000") == 0);
    assert(snorr::sdDue(0, 0, 0, 10, false));
    assert(!snorr::sdDue(1, 100000, 400000, 60, true));
    assert(!snorr::sdDue(1, 0, 400000, 60, false));
    assert(!snorr::sdDue(1, 100000, 100100, 60, false));
    assert(snorr::sdDue(1, 100000, 186400, 60, false));
    assert(snorr::sdDue(1, 0, 0, 86400, false));
}
