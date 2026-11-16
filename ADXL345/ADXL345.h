/*
 * ADXL345.h
 *
 * Driver for the Analog Devices ADXL345 3-axis accelerometer over I2C.
 * Register map, scale factors and the device ID follow the ADXL345
 * datasheet (Rev. E): Table 19 for the register map, Table 1 for the
 * sensitivity figures.
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#ifndef EEN1071_ADXL345_H_
#define EEN1071_ADXL345_H_

#include "I2CDevice.h"

namespace een1071 {

class ADXL345 : public I2CDevice {
public:
    /** ALT ADDRESS pin tied low gives 0x53; tied high gives 0x1D. */
    static constexpr std::uint8_t kDefaultAddress = 0x53;
    /** Fixed value in DEVID (0x00) - a cheap "is the part really there" check. */
    static constexpr std::uint8_t kDeviceId = 0xE5;

    /** Datasheet Table 19. Registers 0x01-0x1C are reserved. */
    enum class Register : std::uint8_t {
        DeviceId      = 0x00,
        ThreshTap     = 0x1D,
        OffsetX       = 0x1E,
        OffsetY       = 0x1F,
        OffsetZ       = 0x20,
        TapDuration   = 0x21,
        TapLatency    = 0x22,
        TapWindow     = 0x23,
        ThreshActive  = 0x24,
        ThreshInact   = 0x25,
        TimeInact     = 0x26,
        ActInactCtl   = 0x27,
        ThreshFreeFall= 0x28,
        TimeFreeFall  = 0x29,
        TapAxes       = 0x2A,
        ActTapStatus  = 0x2B,
        BwRate        = 0x2C,
        PowerCtl      = 0x2D,
        IntEnable     = 0x2E,
        IntMap        = 0x2F,
        IntSource     = 0x30,
        DataFormat    = 0x31,
        DataX0        = 0x32,   /* 0x32-0x37 hold X, Y, Z as LSB-first pairs */
        DataX1        = 0x33,
        DataY0        = 0x34,
        DataY1        = 0x35,
        DataZ0        = 0x36,
        DataZ1        = 0x37,
        FifoCtl       = 0x38,
        FifoStatus    = 0x39
    };

    /** Measurement range, DATA_FORMAT bits D1:D0. */
    enum class Range : std::uint8_t {
        G2  = 0b00,
        G4  = 0b01,
        G8  = 0b10,
        G16 = 0b11
    };

    /** DATA_FORMAT bit D3 (FULL_RES).
     *  TenBit  - fixed 10-bit output, so sensitivity halves as the range doubles.
     *  Full    - resolution grows with the range, sensitivity stays 4 mg/LSB. */
    enum class Resolution : std::uint8_t {
        TenBit = 0,
        Full   = 1
    };

    /** Output data rate, BW_RATE bits D3:D0 (datasheet Table 7). */
    enum class DataRate : std::uint8_t {
        Hz12_5 = 0b0111,
        Hz25   = 0b1000,
        Hz50   = 0b1001,
        Hz100  = 0b1010,   /* power-on default */
        Hz200  = 0b1011,
        Hz400  = 0b1100,
        Hz800  = 0b1101,
        Hz1600 = 0b1110,
        Hz3200 = 0b1111
    };

    /** Raw two's-complement counts straight out of 0x32-0x37. */
    struct Sample {
        std::int16_t x = 0;
        std::int16_t y = 0;
        std::int16_t z = 0;
    };

    /** The same reading scaled to g. */
    struct Acceleration {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /** Tilt relative to the horizontal plane, in degrees, each -90..+90. */
    struct Orientation {
        float pitch = 0.0f;
        float roll  = 0.0f;
    };

    explicit ADXL345(unsigned int bus, std::uint8_t address = kDefaultAddress);

    /** Opens the bus, checks DEVID, applies range/resolution/rate and
     *  leaves standby for measurement mode. Call this once before reading.
     *  @return false if the bus will not open or DEVID is not 0xE5. */
    bool begin(Range range = Range::G2,
               Resolution resolution = Resolution::Full,
               DataRate rate = DataRate::Hz100);

    /** Reads DEVID and compares it with 0xE5. */
    bool verifyDeviceId();

    bool setRange(Range range);
    bool setResolution(Resolution resolution);
    bool setDataRate(DataRate rate);

    /** POWER_CTL measure bit: false parks the part in standby (~0.1 uA). */
    bool setMeasuring(bool enabled);

    /** One burst read of all six data registers. The datasheet requires
     *  the six bytes to be fetched in a single multi-byte transaction,
     *  otherwise an axis can update part-way through and the sample is
     *  skewed. */
    bool readSample(Sample& out);

    /** readSample() scaled by the current sensitivity. */
    bool readAcceleration(Acceleration& out);

    /** Pitch and roll derived from one acceleration reading. Valid only
     *  while the device is roughly static - any linear acceleration is
     *  indistinguishable from a change in tilt. */
    bool readOrientation(Orientation& out);

    /** g per LSB for the current configuration. */
    float sensitivity() const;

    Range range() const noexcept { return range_; }
    Resolution resolution() const noexcept { return resolution_; }

    /** Converts a reading to pitch/roll without touching the bus. */
    static Orientation toOrientation(const Acceleration& acceleration);

private:
    /** Writes the cached range and resolution into DATA_FORMAT in one go,
     *  preserving the bits this driver does not manage (self-test, SPI
     *  mode, interrupt polarity, justification). */
    bool writeDataFormat();

    Range range_;
    Resolution resolution_;
};

} /* namespace een1071 */

#endif /* EEN1071_ADXL345_H_ */
