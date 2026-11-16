/*
 * ADXL345.cpp
 *
 * Implementation of the ADXL345 driver declared in ADXL345.h.
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#include "ADXL345.h"

#include <cmath>
#include <iostream>

namespace een1071 {

namespace {

constexpr float kRadiansToDegrees = 57.29577951308232f;   /* 180 / pi */

/* DATA_FORMAT (0x31) bit positions this driver manages. */
constexpr std::uint8_t kFullResBit   = 1u << 3;
constexpr std::uint8_t kRangeMask    = 0x03;

/* POWER_CTL (0x2D) bit 3 leaves standby and starts conversions. */
constexpr std::uint8_t kMeasureBit   = 1u << 3;

/* BW_RATE (0x2C) bits 3:0 select the output data rate. */
constexpr std::uint8_t kRateMask     = 0x0F;

inline std::uint8_t addressOf(ADXL345::Register reg)
{
    return static_cast<std::uint8_t>(reg);
}

/* Each axis arrives little-endian across two registers. */
inline std::int16_t combine(std::uint8_t low, std::uint8_t high)
{
    const std::uint16_t raw =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(high) << 8 | low);
    return static_cast<std::int16_t>(raw);
}

} /* anonymous namespace */

ADXL345::ADXL345(unsigned int bus, std::uint8_t address)
    : I2CDevice(bus, address),
      range_(Range::G2),
      resolution_(Resolution::Full)
{
}

bool ADXL345::begin(Range range, Resolution resolution, DataRate rate)
{
    if (!this->open()) {
        return false;
    }
    if (!this->verifyDeviceId()) {
        return false;
    }

    range_      = range;
    resolution_ = resolution;

    /* Configure while still in standby, then start measuring. */
    if (!this->setDataRate(rate))   return false;
    if (!this->writeDataFormat())   return false;
    if (!this->setMeasuring(true))  return false;

    return true;
}

bool ADXL345::verifyDeviceId()
{
    std::uint8_t id = 0;
    if (!this->readRegister(addressOf(Register::DeviceId), id)) {
        return false;
    }
    if (id != kDeviceId) {
        std::cerr << "ADXL345: wrong device ID (expected 0xE5, got 0x"
                  << std::hex << static_cast<int>(id) << std::dec
                  << ") - check the address pin and wiring\n";
        return false;
    }
    return true;
}

bool ADXL345::writeDataFormat()
{
    std::uint8_t current = 0;
    if (!this->readRegister(addressOf(Register::DataFormat), current)) {
        return false;
    }

    /* Preserve self-test, SPI mode, INT polarity and justify bits. */
    std::uint8_t updated =
        static_cast<std::uint8_t>(current & ~(kFullResBit | kRangeMask));

    updated = static_cast<std::uint8_t>(updated |
                  static_cast<std::uint8_t>(range_));
    if (resolution_ == Resolution::Full) {
        updated = static_cast<std::uint8_t>(updated | kFullResBit);
    }

    return this->writeRegister(addressOf(Register::DataFormat), updated);
}

bool ADXL345::setRange(Range range)
{
    range_ = range;
    return this->writeDataFormat();
}

bool ADXL345::setResolution(Resolution resolution)
{
    resolution_ = resolution;
    return this->writeDataFormat();
}

bool ADXL345::setDataRate(DataRate rate)
{
    std::uint8_t current = 0;
    if (!this->readRegister(addressOf(Register::BwRate), current)) {
        return false;
    }

    const std::uint8_t updated = static_cast<std::uint8_t>(
        (current & ~kRateMask) | (static_cast<std::uint8_t>(rate) & kRateMask));

    return this->writeRegister(addressOf(Register::BwRate), updated);
}

bool ADXL345::setMeasuring(bool enabled)
{
    std::uint8_t current = 0;
    if (!this->readRegister(addressOf(Register::PowerCtl), current)) {
        return false;
    }

    const std::uint8_t updated = enabled
        ? static_cast<std::uint8_t>(current | kMeasureBit)
        : static_cast<std::uint8_t>(current & ~kMeasureBit);

    return this->writeRegister(addressOf(Register::PowerCtl), updated);
}

float ADXL345::sensitivity() const
{
    /* In FULL_RES the part adds bits as the range grows, so sensitivity is
     * constant at 256 LSB/g (datasheet Table 1). In fixed 10-bit mode the
     * full-scale span is spread over 1024 counts instead, so sensitivity
     * halves every time the range doubles. */
    if (resolution_ == Resolution::Full) {
        return 1.0f / 256.0f;
    }

    float spanG = 4.0f;                      /* +/- 2 g */
    switch (range_) {
        case Range::G4:  spanG = 8.0f;  break;
        case Range::G8:  spanG = 16.0f; break;
        case Range::G16: spanG = 32.0f; break;
        case Range::G2:
        default:         spanG = 4.0f;  break;
    }
    return spanG / 1024.0f;
}

bool ADXL345::readSample(Sample& out)
{
    std::uint8_t raw[6] = { 0, 0, 0, 0, 0, 0 };

    /* One transaction for all three axes: DATAX0 through DATAZ1. */
    if (!this->readRegisters(addressOf(Register::DataX0), raw, sizeof(raw))) {
        return false;
    }

    out.x = combine(raw[0], raw[1]);
    out.y = combine(raw[2], raw[3]);
    out.z = combine(raw[4], raw[5]);
    return true;
}

bool ADXL345::readAcceleration(Acceleration& out)
{
    Sample sample;
    if (!this->readSample(sample)) {
        return false;
    }

    const float scale = this->sensitivity();
    out.x = static_cast<float>(sample.x) * scale;
    out.y = static_cast<float>(sample.y) * scale;
    out.z = static_cast<float>(sample.z) * scale;
    return true;
}

ADXL345::Orientation ADXL345::toOrientation(const Acceleration& acceleration)
{
    const float x = acceleration.x;
    const float y = acceleration.y;
    const float z = acceleration.z;

    /* atan2 against a non-negative second argument keeps the result in
     * -90..+90 and, unlike a plain division, stays defined when the other
     * two axes read zero. */
    Orientation orientation;
    orientation.pitch = std::atan2(x, std::sqrt(y * y + z * z)) * kRadiansToDegrees;
    orientation.roll  = std::atan2(y, std::sqrt(x * x + z * z)) * kRadiansToDegrees;
    return orientation;
}

bool ADXL345::readOrientation(Orientation& out)
{
    Acceleration acceleration;
    if (!this->readAcceleration(acceleration)) {
        return false;
    }
    out = ADXL345::toOrientation(acceleration);
    return true;
}

} /* namespace een1071 */
