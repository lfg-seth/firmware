#pragma once
#include "configuration.h"
#ifdef SNORR_DIRECT_SURVEY
#include "SurveyWire.h"
#include "mesh/mesh-pb-constants.h"

namespace survey {
void setup();
void poll();
bool command(const uint8_t *buf, size_t len);
size_t pop(uint8_t *buf, size_t limit);
void fix(const meshtastic_Position &pos, uint32_t age, uint16_t millisPart,
         bool altitude, bool satellites);
bool intercept(meshtastic_MeshPacket &p);
void receive(const meshtastic_MeshPacket &p);
void classify(meshtastic_MeshPacket &p, bool local);
bool suppress(const meshtastic_MeshPacket &p);
void txStart(const meshtastic_MeshPacket &p, size_t bytes,
             uint32_t predictedMs);
void txDone(const meshtastic_MeshPacket &p);
} // namespace survey
#endif
