#pragma once
#include "configuration.h"
inline struct TestService {
    int sent = 0;
    meshtastic_MeshPacket last;
    void sendToMesh(meshtastic_MeshPacket *p, int, bool)
    {
        ++sent;
        last = *p;
        delete p;
    }
    void releaseToPool(meshtastic_MeshPacket *p) { delete p; }
} testService;
inline auto service = &testService;
