#pragma once
#include "configuration.h"
namespace concurrency
{
class OSThread
{
  public:
    OSThread(const char *) {}
    void setIntervalFromNow(uint32_t) {}
    virtual int32_t runOnce() = 0;
};
} // namespace concurrency
