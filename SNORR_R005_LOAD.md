# R005 dummy-load bench build

Build `pio run -e rak4631-r005-load` from the current `snorr/rak-survey`
source. This explicitly selected target inherits the RAK AHT, INA219 and
passive-neighbor support. Ordinary `rak4631` builds do not enable the load.

At Arduino variant initialization, drive RAK19007 AIN1 (RAK4631 P0.31,
`PIN_A1` / GPIO31) HIGH as an output. R005's owner reports a 2 kΩ series
resistor between this pin and a 2N2222 base controlling a dummy load.
There is no PWM, timer, runtime toggle, or automatic load regulation.
The GPIO remains asserted during normal firmware operation; reset,
bootloader, shutdown and loss of power are outside that guarantee.
Do not assign another module/peripheral to GPIO31 on this assembly.

Pin references: [RAK4631 datasheet](https://docs.rakwireless.com/product-categories/wisblock/rak4631/datasheet/),
and this target's `variant.h` / identity `g_ADigitalPinMap` in `variant.cpp`.
The board's AIN1 label is not Nordic analog channel AIN1; it maps to
P0.31 / Nordic AIN7. Battery sensing stays on P0.05.

Firmware/version and saved configuration readback cannot establish physical
load current or transistor temperature. Verify those independently at the
bench. INA219 wiring, calibration and packet channel interpretation remain
separate checks. Keep profiles, keys and raw sensor data outside Git.
