#pragma once

#include "configuration.h"

#if USERPREFS_SNORR_NEIGHBOR_SIGNALS
#include "SinglePortModule.h"
#include "SnorrNeighborSignals.h"
#include "concurrency/OSThread.h"

class SnorrNeighborModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    SnorrNeighborModule();

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &p) override;
    int32_t runOnce() override;

  private:
    snorr::NeighborSignals signals;
    uint32_t bootId;
    uint32_t sequence = 0;
    uint32_t lastReportMs = 0;
    uint32_t reportIntervalMs = 0;
};
#endif
