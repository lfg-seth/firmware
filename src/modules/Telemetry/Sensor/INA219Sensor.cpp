#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_INA219.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA219Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_INA219.h>

#ifdef SNORR_INA219_DIAGNOSTICS
#include <Wire.h>

static void logINA219Registers(uint8_t address, TwoWire *wire, const char *phase)
{
    uint16_t regs[6] = {};
    for (uint8_t reg = 0; reg < 6; ++reg) {
        wire->beginTransmission(address);
        wire->write(reg);
        const uint8_t error = wire->endTransmission(false);
        if (error || wire->requestFrom(address, static_cast<uint8_t>(2)) != 2) {
            LOG_WARN("[DEBUG-ina219] phase=%s addr=0x%02x reg=%u i2c_error=%u", phase, address, reg, error);
            return;
        }
        regs[reg] = static_cast<uint16_t>(wire->read()) << 8;
        regs[reg] |= wire->read();
    }
    LOG_INFO("[DEBUG-ina219] phase=%s addr=0x%02x cfg=%04x shunt=%04x bus=%04x power=%04x current=%04x cal=%04x", phase, address,
             regs[0], regs[1], regs[2], regs[3], regs[4], regs[5]);
    LOG_INFO("[DEBUG-ina219] shunt_mV=%.3f bus_V=%.3f current_mA=%.3f shunt_R100_mA=%.3f overflow=%u",
             static_cast<int16_t>(regs[1]) * 0.01f, (regs[2] >> 3) * 0.004f, static_cast<int16_t>(regs[4]) * 0.1f,
             static_cast<int16_t>(regs[1]) * 0.1f, regs[2] & 1);
}
#endif

#ifndef INA219_MULTIPLIER
#define INA219_MULTIPLIER 1.0f
#endif

INA219Sensor::INA219Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA219, "INA219") {}

int32_t INA219Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (!ina219.success()) {
        ina219 = Adafruit_INA219(nodeTelemetrySensorsMap[sensorType].first);
        status = ina219.begin(nodeTelemetrySensorsMap[sensorType].second);
    } else {
        status = ina219.success();
    }
    return initI2CSensor();
}

void INA219Sensor::setup() {}

bool INA219Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (measurement->which_variant != meshtastic_Telemetry_environment_metrics_tag &&
        measurement->which_variant != meshtastic_Telemetry_power_metrics_tag)
        return false;

#ifdef SNORR_INA219_DIAGNOSTICS
    const auto sensor = nodeTelemetrySensorsMap[sensorType];
    logINA219Registers(sensor.first, sensor.second, "before");
#endif
    const float voltage = ina219.getBusVoltage_V();
    if (!ina219.success())
        return false;
    const float current = ina219.getCurrent_mA() * INA219_MULTIPLIER;
    if (!ina219.success())
        return false;
#ifdef SNORR_INA219_DIAGNOSTICS
    logINA219Registers(sensor.first, sensor.second, "after");
    LOG_INFO("[DEBUG-ina219] telemetry_V=%.3f telemetry_mA=%.3f", voltage, current);
#endif
    if (measurement->which_variant == meshtastic_Telemetry_power_metrics_tag) {
        measurement->variant.power_metrics.has_ch1_voltage = true;
        measurement->variant.power_metrics.has_ch1_current = true;
        measurement->variant.power_metrics.ch1_voltage = voltage;
        measurement->variant.power_metrics.ch1_current = current;
    } else {
        measurement->variant.environment_metrics.has_voltage = true;
        measurement->variant.environment_metrics.has_current = true;
        measurement->variant.environment_metrics.voltage = voltage;
        measurement->variant.environment_metrics.current = current;
    }
    return true;
}

uint16_t INA219Sensor::getBusVoltageMv()
{
    return lround(ina219.getBusVoltage_V() * 1000);
}

int16_t INA219Sensor::getCurrentMa()
{
    return lround(ina219.getCurrent_mA() * INA219_MULTIPLIER);
}

#endif
