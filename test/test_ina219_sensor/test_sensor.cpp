#include "INA219Sensor.h"
#include <cassert>
#include <cstdio>

static INA219Sensor sensor;

int main()
{
    nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA219] = {0x40, &Wire};
    sensor.runOnce();
    assert(registers[5] == 4096);
    assert(registers[0] == 0x399f);
    registers[2] = (4200 / 4) << 3;

    for (int16_t raw : {0, 80, -80, 160, -160, 500, -500, 10000, -10000}) {
        shuntRaw = raw;
        const float expected = raw * 0.1f * INA219_MULTIPLIER;
        meshtastic_Telemetry environment = meshtastic_Telemetry_init_zero;
        environment.which_variant = meshtastic_Telemetry_environment_metrics_tag;
        assert(sensor.getMetrics(&environment));
        assert(environment.variant.environment_metrics.has_current);
        assert(environment.variant.environment_metrics.has_voltage);
        assert(std::abs(environment.variant.environment_metrics.current - expected) < 0.01f);
        assert(std::abs(environment.variant.environment_metrics.voltage - 4.2f) < 0.001f);
        std::printf("R100 raw=%d -> environment=%g mA (expected %g)\n", raw, environment.variant.environment_metrics.current,
                    expected);
        std::fflush(stdout);

        meshtastic_Telemetry power = meshtastic_Telemetry_init_zero;
        power.which_variant = meshtastic_Telemetry_power_metrics_tag;
        assert(sensor.getMetrics(&power));
        assert(power.variant.power_metrics.has_ch1_current);
        assert(power.variant.power_metrics.has_ch1_voltage);
        assert(std::abs(power.variant.power_metrics.ch1_current - expected) < 0.01f);
        assert(std::abs(power.variant.power_metrics.ch1_voltage - 4.2f) < 0.001f);
        assert(!power.variant.power_metrics.has_ch2_current);
        assert(!power.variant.power_metrics.has_ch3_current);
        assert(sensor.getCurrentMa() == lround(expected));
        assert(sensor.getBusVoltageMv() == 4200);
    }
    meshtastic_Telemetry unsupported = meshtastic_Telemetry_init_zero;
    unsupported.which_variant = meshtastic_Telemetry_device_metrics_tag;
    assert(!sensor.getMetrics(&unsupported));
    assert(!unsupported.variant.device_metrics.has_voltage);
    for (int failedReg : {2, 4}) {
        failedRegister = failedReg;
        meshtastic_Telemetry failed = meshtastic_Telemetry_init_zero;
        failed.which_variant = meshtastic_Telemetry_power_metrics_tag;
        assert(!sensor.getMetrics(&failed));
        assert(!failed.variant.power_metrics.has_ch1_current);
        assert(!failed.variant.power_metrics.has_ch1_voltage);
        failed = meshtastic_Telemetry_init_zero;
        failed.which_variant = meshtastic_Telemetry_environment_metrics_tag;
        assert(!sensor.getMetrics(&failed));
        assert(!failed.variant.environment_metrics.has_current);
        assert(!failed.variant.environment_metrics.has_voltage);
    }
    failedRegister = -1;
    std::puts("PASS: R100 calibration, signed current, telemetry union and "
              "multiplier consistency");
}
