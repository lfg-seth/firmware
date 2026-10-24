# R004 INA219 register diagnostic

The owner authorized diagnostic flashing of R004 on 2026-10-21 for
[the current discrepancy investigation](https://github.com/lfg-seth/snorr-meshlink/issues/4).
Build `pio run -e rak4631-ina219-debug`. Ordinary targets omit this logging.

Each INA219 telemetry sample logs registers 0–5 before and after the normal
Adafruit reads, including I2C transport failures. It also temporarily selects
triggered shunt-plus-bus conversion, waits 3 ms for the driver's 12-bit preset,
and reads the held registers three times. It restores the original sensor
configuration before the normal telemetry reads. The normal library still
refreshes its calibration register. No calibration correction is applied.

Continuous snapshots are sequential while conversion continues. The three
`single` snapshots should retain shunt/current/voltage words; reading power
clears the bus conversion-ready flag. Changed held values distinguish a read
or device problem from changing inputs during ordinary continuous conversion.

`[DEBUG-ina219]` reports raw configuration, signed shunt/current, bus, power
and calibration words. The displayed conversions assume the existing
0.1-ohm R100 / 0.1 mA current-LSB preset. Raw words remain authoritative if
configuration/calibration differ. Compare shunt-derived current with the
current register and telemetry; neither validates the physical shunt voltage.

Keep full serial logs and profile/key backups private. Confirm board identity,
back up configuration, verify the image hash and independently compare the
complete profile after flashing. This target does not enable power telemetry,
change intervals, reset identity or validate hardware accuracy. Restore an
ordinary image when diagnostic collection is complete.
