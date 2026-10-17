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

int main()
{
    TestModule module;
    meshtastic_MeshPacket incoming;
    assert(module.wantPacket(&incoming));
    incoming.channel = 1;
    assert(!module.wantPacket(&incoming));
    incoming.channel = 0;
    incoming.via_mqtt = true;
    assert(!module.wantPacket(&incoming));
    incoming.via_mqtt = false;
    module.handleReceived(incoming);

    auto deferred = [&]() {
        module.runOnce();
        assert(testService.sent == 0);
    };
    moduleConfig.neighbor_info.enabled = false;
    assert(!module.wantPacket(&incoming));
    deferred();
    moduleConfig.neighbor_info.enabled = true;
    channels.isDefault = true;
    deferred();
    channels.isDefault = false;
    config.lora.tx_enabled = false;
    deferred();
    config.lora.tx_enabled = true;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
    deferred();
    config.device.role = 0;
    testAirTime.channelAllowed = false;
    deferred();
    testAirTime.channelAllowed = true;
    testAirTime.airAllowed = false;
    deferred();
    testAirTime.airAllowed = true;
    module.runOnce();
    assert(testService.sent == 1);
    assert(testService.last.to == NODENUM_BROADCAST);
    assert(testService.last.priority == meshtastic_MeshPacket_Priority_BACKGROUND);
    assert(!testService.last.want_ack && !testService.last.decoded.want_response);
    assert(testService.last.decoded.payload.size == 40);

    testNow += 299999;
    module.handleReceived(incoming);
    module.runOnce();
    assert(testService.sent == 1);
    ++testNow;
    module.runOnce();
    assert(testService.sent == 2);

    TestModule local;
    moduleConfig.neighbor_info.transmit_over_lora = false;
    config.lora.tx_enabled = false;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
    local.handleReceived(incoming);
    local.runOnce();
    assert(testService.sent == 3 && testService.last.to == NODENUM_BROADCAST_NO_LORA);
    testNow += 59999;
    local.handleReceived(incoming);
    local.runOnce();
    assert(testService.sent == 3);
    ++testNow;
    local.runOnce();
    assert(testService.sent == 4);

    moduleConfig.neighbor_info.transmit_over_lora = true;
    config.lora.tx_enabled = true;
    config.device.role = 0;
    testNow += 60000;
    local.handleReceived(incoming);
    local.runOnce();
    assert(testService.sent == 4);
    testNow += 240000;
    local.runOnce();
    assert(testService.sent == 5 && testService.last.to == NODENUM_BROADCAST);

    TestModule rollover;
    testNow = UINT32_MAX - 1000;
    rollover.handleReceived(incoming);
    rollover.runOnce();
    assert(testService.sent == 6);
    testNow += 299999;
    rollover.handleReceived(incoming);
    rollover.runOnce();
    assert(testService.sent == 6);
    ++testNow;
    rollover.runOnce();
    assert(testService.sent == 7);
    testNow += snorr::NeighborSignals::EXPIRY_MS;
    rollover.runOnce();
    assert(testService.sent == 7);
    std::puts("PASS: actual module filters, transmit gates, priority, local mode and cooldowns");
}
#endif
