#pragma once
#include "configuration.h"
struct Throttle {
    static bool isWithinTimespanMs(uint32_t lastMs, uint32_t intervalMs) { return testNow - lastMs < intervalMs; }
};
