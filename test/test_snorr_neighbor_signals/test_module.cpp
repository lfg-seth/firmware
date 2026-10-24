#ifdef SNORR_STANDALONE_MODULE_TEST
#include "modules/SnorrNeighborModule.cpp"
#include <cassert>
#include <cstdio>

class TestModule : public SnorrNeighborModule
{
  public:
    using SnorrNeighborModule::handleReceived;
    using SnorrNeighborModule::runOnce;
    using SnorrNeighborModule::wantPacket;
};

static void reset()
{
    testNow = 100000;
    testRandomOffset = 0;
    testService = {};
    moduleConfig = {};
    config = {};
    channels = {};
    testAirTime = {};
}

static void hear(TestModule &module)
{
    meshtastic_MeshPacket incoming;
    assert(module.wantPacket(&incoming));
    module.handleReceived(incoming);
}

static void expectSend(TestModule &module, int count)
{
    assert(module.runOnce() == 15000);
    assert(testService.sent == count);
}

static uint32_t read32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

static void testSavedIntervals()
{
    struct Case {
        uint32_t saved;
        uint32_t baselineMs;
    };
    for (const auto &input :
         {Case{0, USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS * 1000}, Case{1, 180000}, Case{179, 180000}, Case{180, 180000},
          Case{300, 300000}, Case{900, 900000}, Case{3601, 3601000}, Case{86400, 86400000}, Case{2147468, 2147468000},
          Case{2147469, 2147468000}, Case{4294968, 2147468000}, Case{UINT32_MAX, 2147468000}}) {
        reset();
        moduleConfig.telemetry.device_update_interval = input.saved;
        moduleConfig.telemetry.environment_update_interval = 1;
        moduleConfig.telemetry.power_update_interval = UINT32_MAX;
        TestModule module;
        hear(module);
        testRandomOffset = 14999;
        expectSend(module, 1);
        testRandomOffset = 0;
        testNow += input.baselineMs;
        hear(module);
        expectSend(module, 1);
        testNow += 14998;
        expectSend(module, 1);
        ++testNow;
        expectSend(module, 2);
        hear(module);
        expectSend(module, 2);
    }
}

static void testConfigurationChanges()
{
    reset();
    moduleConfig.telemetry.device_update_interval = 900;
    TestModule shorter;
    hear(shorter);
    testRandomOffset = 14999;
    expectSend(shorter, 1);
    moduleConfig.telemetry.device_update_interval = 180;
    testNow += 180000;
    hear(shorter);
    expectSend(shorter, 1);
    testNow += 720000 + 14998;
    hear(shorter);
    expectSend(shorter, 1);
    ++testNow;
    expectSend(shorter, 2);
    testNow += 194998;
    hear(shorter);
    expectSend(shorter, 2);
    ++testNow;
    expectSend(shorter, 3);

    reset();
    moduleConfig.telemetry.device_update_interval = 180;
    TestModule longer;
    hear(longer);
    testRandomOffset = 14999;
    expectSend(longer, 1);
    testNow += 100000;
    moduleConfig.telemetry.device_update_interval = 900;
    hear(longer);
    expectSend(longer, 1);
    testNow += 800000 + 14998;
    hear(longer);
    expectSend(longer, 1);
    ++testNow;
    expectSend(longer, 2);
    testNow += 3600000;
    hear(longer);
    expectSend(longer, 3);
    hear(longer);
    expectSend(longer, 3);
    moduleConfig.neighbor_info.enabled = false;
    testNow += 1000;
    expectSend(longer, 3);
    moduleConfig.neighbor_info.enabled = true;
    expectSend(longer, 3);
}

static void testModes()
{
    reset();
    moduleConfig.telemetry.device_update_interval = UINT32_MAX;
    moduleConfig.neighbor_info.transmit_over_lora = false;
    config.lora.tx_enabled = false;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
    channels.isDefault = true;
    testAirTime.channelAllowed = testAirTime.airAllowed = false;
    TestModule local;
    hear(local);
    testRandomOffset = 14999;
    expectSend(local, 1);
    assert(testService.last.to == NODENUM_BROADCAST_NO_LORA);
    testNow += 74998;
    hear(local);
    expectSend(local, 1);
    ++testNow;
    expectSend(local, 2);
    assert(!config.lora.tx_enabled);

    moduleConfig.telemetry.device_update_interval = 180;
    moduleConfig.neighbor_info.transmit_over_lora = true;
    config.lora.tx_enabled = true;
    config.device.role = 0;
    channels.isDefault = false;
    testAirTime.channelAllowed = testAirTime.airAllowed = true;
    testNow += 74999;
    hear(local);
    expectSend(local, 2);
    testNow += 120000;
    expectSend(local, 3);
    assert(testService.last.to == NODENUM_BROADCAST);

    moduleConfig.neighbor_info.transmit_over_lora = false;
    config.lora.tx_enabled = false;
    testNow += 74999;
    hear(local);
    expectSend(local, 3);
    testNow += 120000;
    expectSend(local, 4);
    assert(testService.last.to == NODENUM_BROADCAST_NO_LORA);
    testNow += 74999;
    hear(local);
    expectSend(local, 5);
    assert(!config.lora.tx_enabled);
}

static void testRolloverAndReboot()
{
    for (uint32_t start : {UINT32_MAX - 1000, uint32_t(0)}) {
        reset();
        testNow = start;
        moduleConfig.telemetry.device_update_interval = 300;
        TestModule module;
        hear(module);
        testRandomOffset = 14999;
        expectSend(module, 1);
        testNow += 314998;
        hear(module);
        expectSend(module, 1);
        ++testNow;
        expectSend(module, 2);
    }

    reset();
    moduleConfig.telemetry.device_update_interval = 900;
    {
        TestModule beforeReboot;
        hear(beforeReboot);
        expectSend(beforeReboot, 1);
        assert(read32(testService.last.decoded.payload.bytes + 16) == 1);
    }
    testNow = 0;
    testRandomOffset = 14999;
    TestModule afterReboot;
    assert(testStartupDelayMs == 74999);
    testNow = testStartupDelayMs;
    expectSend(afterReboot, 1);
    hear(afterReboot);
    expectSend(afterReboot, 2);
    assert(read32(testService.last.decoded.payload.bytes + 16) == 1);
    assert(read32(testService.last.decoded.payload.bytes + 12) == 15000);
    testNow += 194999;
    hear(afterReboot);
    expectSend(afterReboot, 2);
    testNow += 720000;
    expectSend(afterReboot, 3);
}

static void testFiltersAndGates()
{
    reset();
    moduleConfig.telemetry.device_update_interval = 180;
    TestModule module;
    assert(testStartupDelayMs == 60000);
    expectSend(module, 0);
    meshtastic_MeshPacket incoming;
    for (int mode = 0; mode < 4; ++mode) {
        incoming.channel = mode == 0 ? 1 : 0;
        incoming.via_mqtt = mode == 1;
        incoming.hops = mode == 2 ? 1 : 0;
        incoming.from = mode == 3 ? 1 : 2;
        assert(!module.wantPacket(&incoming));
    }
    hear(module);
    moduleConfig.neighbor_info.enabled = false;
    assert(!module.wantPacket(&incoming));
    expectSend(module, 0);
    moduleConfig.neighbor_info.enabled = true;
    for (int gate = 0; gate < 5; ++gate) {
        channels.isDefault = gate == 0;
        config.lora.tx_enabled = gate != 1;
        config.device.role = gate == 2 ? meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE : 0;
        testAirTime.channelAllowed = gate != 3;
        testAirTime.airAllowed = gate != 4;
        assert(module.runOnce() == 30000);
        assert(testService.sent == 0);
    }
    testAirTime.airAllowed = true;
    expectSend(module, 1);
    assert(testService.last.to == NODENUM_BROADCAST);
    assert(testService.last.priority == meshtastic_MeshPacket_Priority_BACKGROUND);
    assert(!testService.last.want_ack && !testService.last.decoded.want_response);
    assert(testService.last.decoded.payload.size == 40);
    hear(module);
    testNow += 180000;
    testAirTime.airAllowed = false;
    assert(module.runOnce() == 30000);
    assert(testService.sent == 1);
    testAirTime.airAllowed = true;
    expectSend(module, 2);
    hear(module);
    testNow += snorr::NeighborSignals::EXPIRY_MS;
    expectSend(module, 2);
}

int main()
{
    testSavedIntervals();
    testConfigurationChanges();
    testModes();
    testRolloverAndReboot();
    testFiltersAndGates();
    std::puts("PASS: actual SNNS module + Throttle: saved intervals, saturation, "
              "changes, "
              "modes, reboot, rollover, jitter, filters and airtime gates");
}
#endif
