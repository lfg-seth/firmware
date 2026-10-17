#pragma once
#include "configuration.h"
enum class ProcessMessage { CONTINUE };
class SinglePortModule
{
  public:
    SinglePortModule(const char *, int) {}
    bool isPromiscuous = false;
    virtual bool wantPacket(const meshtastic_MeshPacket *) = 0;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &) = 0;
    meshtastic_MeshPacket *allocDataPacket() { return new meshtastic_MeshPacket{}; }
};
