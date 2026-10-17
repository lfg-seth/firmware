"""Compile the unchanged sensor/library sources against an emulated R100 device.

Requires g++, and PlatformIO's cached Adafruit INA219 and Nanopb dependencies.
Only Meshtastic runtime services and the I2C transport are stubbed.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--env", default="rak4631")
args = parser.parse_args()
deps = ROOT / ".pio/libdeps" / args.env
library = deps / "Adafruit INA219"
assert (library / "Adafruit_INA219.cpp").is_file(), "Cache the board dependencies first"

with tempfile.TemporaryDirectory() as directory:
    stage = Path(directory)
    sensor = stage / "Sensor"
    sensor.mkdir()
    for name in (
        "INA219Sensor.cpp",
        "INA219Sensor.h",
        "CurrentSensor.h",
        "VoltageSensor.h",
    ):
        shutil.copyfile(ROOT / "src/modules/Telemetry/Sensor" / name, sensor / name)
    proto = stage / "mesh/generated/meshtastic"
    proto.mkdir(parents=True)
    (proto / "telemetry.pb.h").write_text(
        f'#include "{(ROOT / "src/mesh/generated/meshtastic/telemetry.pb.h").as_posix()}"\n'
    )
    (stage / "Arduino.h").write_text(
        "#pragma once\n#include <cstdint>\n#include <cstddef>\n#define MSBFIRST 1\n"
    )
    (stage / "Wire.h").write_text(
        "#pragma once\nclass TwoWire {};\ninline TwoWire Wire;\n"
    )
    (stage / "Adafruit_I2CDevice.h").write_text("""#pragma once
#include "Arduino.h"
#include "Wire.h"
inline uint16_t registers[6]{};
inline int16_t shuntRaw = 0;
inline bool readOK = true;
inline int failedRegister = -1;
class Adafruit_I2CDevice {
public:
    Adafruit_I2CDevice(uint8_t, TwoWire *) {}
    bool begin() { return true; }
};
""")
    (stage / "Adafruit_BusIO_Register.h").write_text("""#pragma once
#include "Adafruit_I2CDevice.h"
class Adafruit_BusIO_Register {
    uint8_t reg;
public:
    Adafruit_BusIO_Register(Adafruit_I2CDevice *, uint8_t r, int, int) : reg(r) {}
    bool read(uint16_t *value) {
        if (!readOK || reg == failedRegister) return false;
        if (reg == 1) *value = shuntRaw;
        else if (reg == 4) *value = int16_t(int32_t(shuntRaw) * registers[5] / 4096);
        else *value = registers[reg];
        return true;
    }
    bool write(uint32_t value, int = 2) { registers[reg] = value; return true; }
};
class Adafruit_BusIO_RegisterBits {
public:
    Adafruit_BusIO_RegisterBits(Adafruit_BusIO_Register *, int, int) {}
    bool write(uint32_t) { return true; }
};
""")
    (stage / "configuration.h").write_text("""#pragma once
#include <cmath>
#include <cstdint>
#define HAS_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR 0
#define LOG_INFO(...) ((void)0)
""")
    (sensor / "TelemetrySensor.h").write_text("""#pragma once
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Wire.h"
#include <utility>
constexpr int DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS = 1000;
inline std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[50]{};
class TelemetrySensor {
protected:
    unsigned status = 0;
    int sensorType;
    virtual void setup() {}
    int32_t initI2CSensor() { if (status) setup(); return 1000; }
public:
    const char *sensorName;
    TelemetrySensor(int type, const char *name) : sensorType(type), sensorName(name) {}
    bool hasSensor() { return nodeTelemetrySensorsMap[sensorType].first != 0; }
    virtual int32_t runOnce() = 0;
    virtual bool getMetrics(meshtastic_Telemetry *) = 0;
};
""")
    for multiplier in (1, 10):
        executable = stage / f"test-{multiplier}"
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                f"-DINA219_MULTIPLIER={multiplier}.0f",
                "-I",
                str(stage),
                "-I",
                str(sensor),
                "-I",
                str(library),
                "-I",
                str(deps / "Nanopb"),
                "-I",
                str(ROOT / "src/mesh/generated/meshtastic"),
                str(sensor / "INA219Sensor.cpp"),
                str(library / "Adafruit_INA219.cpp"),
                str(Path(__file__).with_name("test_sensor.cpp")),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
