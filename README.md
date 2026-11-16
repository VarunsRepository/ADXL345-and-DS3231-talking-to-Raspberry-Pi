# ADXL345 + DS3231 over I2C — Raspberry Pi

C++ drivers for an ADXL345 3-axis accelerometer and a DS3231 real-time clock
sharing one I2C bus: tilt sensing with the RTC available as an external
timebase. Coursework for EEN1071.

## What's here

| File | Purpose |
|---|---|
| `I2CDevice.h/.cpp` | Generic I2C device — one slave address on one bus, via `/dev/i2c-N` |
| `ADXL345.h/.cpp` | Accelerometer — range, resolution, data rate, raw/scaled reads, pitch and roll |
| `DS3231.h/.cpp` | RTC — register map, square-wave output, alarm flags, temperature, BCD helpers |
| `application.cpp` | Streams tilt at 10 Hz and prints RTC die temperature once a second |

Both drivers sit on the same `I2CDevice`, so adding a third device on the bus
means one more subclass and nothing else. I2C access goes through the kernel's
`i2c-dev` character device — `open()`, `ioctl()`, `close()` — not
memory-mapped I/O. Register reads are issued as a single `I2C_RDWR`
transaction, so the bus is never released between sending the register index
and reading the data.

## Wiring

| Device | Address | Notes |
|---|---|---|
| ADXL345 | `0x53` | `CS` tied **high** selects I2C; `ALT ADDRESS` low gives `0x53`, high gives `0x1D` |
| DS3231 | `0x68` | `INT#/SQW` optionally to GPIO 17 for a hardware sampling tick |

Both share SDA/SCL on bus 1. Check what the bus sees with
`i2cdetect -y 1`.

The ADXL345 is a dual-interface part and also speaks SPI; this project uses
I2C, which is fine up to a few hundred Hz. SPI would be needed for its
3200 Hz maximum output rate.

## Build

```bash
./build          # -> ./accel_rtc
```

Or directly:

```bash
g++ -std=c++17 -O2 application.cpp I2CDevice.cpp ADXL345.cpp DS3231.cpp -o accel_rtc
```

Needs I2C enabled via `raspi-config`, and the user in the `i2c` group (or run
with `sudo`).

## Notes

Originally scaffolded from the EEN1071 module template; the I2C, ADXL345 and
DS3231 layers here are my own implementation, written against the kernel
i2c-dev interface and the ADXL345 (Rev. E) and DS3231 (Rev. 10) datasheets.

MIT licensed.
