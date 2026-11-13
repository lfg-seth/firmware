#pragma once
#ifdef SNORR_SD_GATEWAY
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>

namespace recorder
{
// IDs are part of the on-card SML1 schema.
enum Kind : uint8_t {
    Boot = 1, RawRx, Decoded, Duplicate, Local, Admission, TxStart, TxDone,
MqttEnvelope, MqttQueued, MqttDiscard, MqttResult, Link, Gap, Clock, Eviction, RouterDiscard, MqttIntent, TxDiscard, WifiSurvey
};
struct Storage {
    uint8_t flags; // card detected, mounted, last write OK, counts valid
    uint64_t used, total;
    uint32_t drops;
};
void begin(uint32_t node);
uint32_t bootId();
Storage storage();
uint32_t event(Kind kind, uint32_t from = 0, uint32_t id = 0, int32_t detail = 0, const void *bytes = nullptr,
               size_t size = 0, uint32_t related = 0, int16_t rssi = 0, int16_t snrQuarter = 0);
void packet(Kind kind, const meshtastic_MeshPacket &p, int32_t detail = 0);
}
#endif
