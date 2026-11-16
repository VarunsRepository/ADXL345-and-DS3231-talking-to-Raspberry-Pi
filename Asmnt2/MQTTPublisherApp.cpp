/*
 * MQTTPublisherApp.cpp
 *
 * Single-shot publisher: takes one SoC temperature reading and one
 * accelerometer reading, publishes them together, exits. Useful from cron
 * or for a quick end-to-end test; the dual-thread version is the one to
 * run for a continuous stream.
 */

#include <cstdio>
#include <ctime>
#include <chrono>
#include <iostream>
#include <string>

#include "MQTTHelper.h"
#include "MQTTPublisher.h"
#include "../ADXL345/ADXL345.h"

int main(int argc, char* argv[])
{
    const std::map<std::string, std::string> args = parseArgs(argc, argv);
    if (!requireArgs(args, {"--address", "--client", "--user", "--pword", "--topic"}, argv[0])) {
        return -1;
    }

    const std::string ADDRESS  = args.at("--address");
    const std::string CLIENTID = args.at("--client");
    const std::string USERNAME = args.at("--user");
    const std::string PASSWORD = args.at("--pword");
    const std::string TOPIC    = args.at("--topic");

    const int QOS = args.count("--qos") ? std::stoi(args.at("--qos")) : 1;
    const int CLEANSESSION = args.count("--cleansession")
                           ? std::stoi(args.at("--cleansession")) : 1;

    // ---- timestamp ----------------------------------------------------
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm timeinfo{};
    char time_str[32] = {0};
    if (localtime_r(&now_time, &timeinfo) != nullptr) {
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    // ---- SoC temperature ----------------------------------------------
    float cpuTemp = 0.0f;
    const bool haveCpuTemp = readCPUTemperature(cpuTemp);
    if (!haveCpuTemp) {
        std::cout << "Warning: could not read " << CPU_TEMP << "\n";
    }

    // ---- accelerometer ------------------------------------------------
    een1071::ADXL345 sensor(1);           // bus 1, default address 0x53
    if (!sensor.begin(een1071::ADXL345::Range::G4,
                      een1071::ADXL345::Resolution::Full,
                      een1071::ADXL345::DataRate::Hz100)) {
        std::cout << "Accelerometer did not initialise - check wiring, CS tied high, "
                     "and i2cdetect -y 1\n";
        return -1;
    }

    een1071::ADXL345::Sample raw;
    een1071::ADXL345::Acceleration acc;
    if (!sensor.readSample(raw) || !sensor.readAcceleration(acc)) {
        std::cout << "Accelerometer read failed\n";
        return -1;
    }

    const een1071::ADXL345::Orientation tilt = een1071::ADXL345::toOrientation(acc);

    std::cout << "pitch " << tilt.pitch << " deg, roll " << tilt.roll
              << " deg, raw (" << raw.x << ", " << raw.y << ", " << raw.z << ")\n";

    // ---- payload ------------------------------------------------------
    // Built once, and it actually carries the sensor data. The previous
    // version called the accelerometer getters, threw the results away,
    // and published a CPU-temperature-only payload twice.
    char payload[320];
    std::snprintf(payload, sizeof(payload),
                  "{\"d\":{"
                  "\"cpuTemp\": %.2f, "
                  "\"pitch\": %.2f, "
                  "\"roll\": %.2f, "
                  "\"accel\": {\"x\": %d, \"y\": %d, \"z\": %d}, "
                  "\"timestamp\": \"%s\""
                  "}}",
                  haveCpuTemp ? cpuTemp : 0.0f,
                  tilt.pitch, tilt.roll,
                  static_cast<int>(raw.x),
                  static_cast<int>(raw.y),
                  static_cast<int>(raw.z),
                  time_str);

    // ---- publish ------------------------------------------------------
    MQTTPublisher mqtt(ADDRESS, CLIENTID, USERNAME, PASSWORD, TOPIC, CLEANSESSION);

    if (!mqtt.connect()) {
        return -1;
    }

    const bool ok = mqtt.publish(payload, QOS);

    mqtt.disconnect();
    return ok ? 0 : -1;
}

// example usage
//   ./MQTTPublisherApp --address tcp://<broker-host>:1883
//                      --client "pi-oneshot"
//                      --user <username> --pword '<password>'
//                      --topic Sensors/ADXL345
