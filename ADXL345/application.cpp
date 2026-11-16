/*
 * application.cpp
 *
 * ADXL345 accelerometer and DS3231 RTC on the same I2C bus: streams tilt
 * at 10 Hz and prints the RTC's die temperature once a second.
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>

#include <iomanip>
#include <iostream>

#include "ADXL345.h"
#include "DS3231.h"

using namespace std;
using namespace een1071;

namespace {

constexpr unsigned int kI2CBus = 1;
constexpr int kSamples = 600;            /* 600 x 100 ms = one minute */
constexpr useconds_t kSamplePeriodUs = 100000;

} /* anonymous namespace */

int main()
{
	cout << "ADXL345 + DS3231 on I2C bus " << kI2CBus << endl;

	ADXL345 sensor(kI2CBus);
	DS3231 rtc(kI2CBus);

	if (!sensor.begin(ADXL345::Range::G4,
	                  ADXL345::Resolution::Full,
	                  ADXL345::DataRate::Hz100)) {
		cerr << "Could not bring up the accelerometer" << endl;
		return 1;
	}

	if (!rtc.open()) {
		cerr << "Could not open the RTC" << endl;
		return 1;
	}

	/* 1 Hz on the RTC's SQW pin, handy as an external sampling tick. */
	rtc.enableSquareWave(DS3231::SquareWave::Hz1);

	bool oscillatorStopped = false;
	if (rtc.oscillatorStoppedSinceLastCheck(oscillatorStopped) && oscillatorStopped) {
		cerr << "Warning: RTC oscillator has stopped since it was last set,"
		        " so the time is not trustworthy" << endl;
	}

	cout << fixed << setprecision(2);

	for (int sample = 0; sample < kSamples; ++sample) {
		ADXL345::Acceleration acceleration;
		if (!sensor.readAcceleration(acceleration)) {
			break;
		}

		const ADXL345::Orientation tilt = ADXL345::toOrientation(acceleration);

		cout << "pitch " << setw(7) << tilt.pitch << " deg"
		     << "  roll " << setw(7) << tilt.roll << " deg"
		     << "  (" << acceleration.x << ", "
		               << acceleration.y << ", "
		               << acceleration.z << ") g";

		if (sample % 10 == 0) {
			float celsius = 0.0f;
			if (rtc.readTemperature(celsius)) {
				cout << "  rtc " << celsius << " C";
			}
		}

		cout << "          \r" << flush;
		usleep(kSamplePeriodUs);
	}

	cout << endl;
	return 0;
}
