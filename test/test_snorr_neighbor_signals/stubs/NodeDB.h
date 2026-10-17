#pragma once
#include "configuration.h"
inline struct TestNodeDB {
    uint32_t getNodeNum() { return 1; }
} testNodeDB;
inline auto nodeDB = &testNodeDB;
inline int getHopsAway(const meshtastic_MeshPacket &p)
{
    return p.hops;
}
