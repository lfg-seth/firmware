#pragma once
#ifdef SNORR_SD_GATEWAY
#include <cstdint>
namespace snorr {
struct WifiSample { uint8_t state; int8_t rssi; uint8_t channel; uint32_t association; };
WifiSample sampleWifi();
void broadcastWifi(uint32_t intervalSeconds);
void recordSurvey(const WifiSample &sample);
}
#endif
