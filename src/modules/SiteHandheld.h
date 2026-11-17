#pragma once
#ifdef SNORR_SITE_HANDHELD
#include "SiteJournal.h"
class OLEDDisplay;
namespace site {
void setup();
void button(bool down);
void draw(OLEDDisplay *display);
bool pending(unsigned ordinal, uint32_t target, uint32_t now);
bool result(unsigned ordinal, uint32_t now, Result value);
void stopped(bool manual);
void fault(const char *message);
unsigned usb(const uint8_t *data, size_t len, uint8_t *reply, size_t capacity);
uint32_t clockTime();
bool setUsbTime(uint32_t epoch);
} // namespace site
namespace survey {
bool fieldReady(uint16_t mask);
bool fieldStart(uint16_t mask, bool saved);
void fieldFinish();
void cancelWalk();
} // namespace survey
#endif
