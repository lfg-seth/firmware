#pragma once
#include "configuration.h"
inline uint32_t testStartupDelayMs = 0;
namespace concurrency
{
class OSThread
{
  public:
    OSThread(const char *) {}
    void setIntervalFromNow(uint32_t delayMs) { testStartupDelayMs = delayMs; }
    virtual int32_t runOnce() = 0;
};
} // namespace concurrency
