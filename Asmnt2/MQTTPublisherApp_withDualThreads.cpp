/*
 * MQTTPublisherApp_withDualThreads.cpp
 *
 * Two publisher threads on one broker: SoC temperature once a second and
 * accelerometer tilt ten times a second, each on its own topic and its own
 * client so the two streams are independent.
 */

#include <csignal>
#include <cstdio>
#include <ctime>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "MQTTHelper.h"
#include "MQTTPublisher.h"
#include "../ADXL345/ADXL345.h"

namespace {

// Set from the signal handler so both threads can leave their loops and be
// joined. A clean disconnect matters here: killing the process abruptly
// makes the broker publish our own Last Will message.
std::atomic<bool> g_running{true};

extern "C" void requestStop(int)
{
    g_running.store(false);
}

// Sleeps in short slices so Ctrl-C is acted on promptly rather than after
// a whole publishing interval.
void interruptibleSleep(std::chrono::milliseconds total)
{
    const std::chrono::milliseconds slice(50);
    std::chrono::milliseconds slept(0);
    while (slept < total && g_running.load()) {
        const std::chrono::milliseconds step = (total - slept) < slice ? (total - slept) : slice;
        std::this_thread::sleep_for(step);
        slept += step;
    }
}

std::string timestampNow()
{
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    // localtime_r, not localtime: this runs on two threads at once.
    std::tm timeinfo{};
    char time_str[32] = {0};
    if (localtime_r(&now_time, &timeinfo) != nullptr) {
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    return std::string(time_str);
}

// Generic publishing loop: generate a payload, send it, wait, repeat.
void publisherThread(MQTTPublisher& mqtt,
                     std::function<std::string()> payloadGenerator,
                     int qos,
                     std::chrono::milliseconds interval)
{
    while (g_running.load()) {
        const std::string payload = payloadGenerator();
        if (!payload.empty()) {
            mqtt.publish(payload, qos);      // takes a std::string: no needless c_str() round trip
        }
        interruptibleSleep(interval);
    }
}

std::string makeCPUPayload()
{
    float temp = 0.0f;
    if (!readCPUTemperature(temp)) {
        std::fprintf(stderr, "Could not read %s - skipping this CPU sample\n", CPU_TEMP);
        return std::string();
    }

    const std::string stamp = timestampNow();

    char buf[200];
    // snprintf, not sprintf: the buffer size is enforced rather than hoped for.
    std::snprintf(buf, sizeof(buf),
                  "{\"d\":{\"cpuTemp\": %.2f, \"timestamp\": \"%s\"}}",
                  temp, stamp.c_str());
    return std::string(buf);
}

std::string makeAccelPayload(een1071::ADXL345& sensor)
{
    een1071::ADXL345::Sample raw;
    een1071::ADXL345::Acceleration acc;

    if (!sensor.readSample(raw) || !sensor.readAcceleration(acc)) {
        std::fprintf(stderr, "Accelerometer read failed - skipping this sample\n");
        return std::string();
    }

    const een1071::ADXL345::Orientation tilt = een1071::ADXL345::toOrientation(acc);

    char buf[300];
    std::snprintf(buf, sizeof(buf),
                  "{ \"d\": {"
                  "\"pitch\": %.2f, "
                  "\"roll\": %.2f, "
                  "\"accel\": {\"x\": %d, \"y\": %d, \"z\": %d}"
                  "} }",
                  tilt.pitch, tilt.roll,
                  static_cast<int>(raw.x),
                  static_cast<int>(raw.y),
                  static_cast<int>(raw.z));

    // Debug line placed AFTER the snprintf. Printing buf before filling it
    // read an uninitialised array as a C string, which is undefined.
    std::printf("accel :: pitch %.2f roll %.2f  raw(%d, %d, %d)  -> %s\n",
                tilt.pitch, tilt.roll,
                static_cast<int>(raw.x), static_cast<int>(raw.y), static_cast<int>(raw.z),
                buf);

    return std::string(buf);
}

} /* anonymous namespace */

int main(int argc, char* argv[])
{
    const std::map<std::string, std::string> args = parseArgs(argc, argv);
    if (!requireArgs(args, {"--address", "--client", "--user", "--pword"}, argv[0])) {
        return -1;
    }

    const std::string ADDRESS  = args.at("--address");
    const std::string CLIENTID = args.at("--client");
    const std::string USERNAME = args.at("--user");
    const std::string PASSWORD = args.at("--pword");

    // --topic is used as the prefix for both streams, so the option is no
    // longer parsed and then ignored.
    const std::string TOPIC_BASE = args.count("--topic") ? args.at("--topic") : "Sensors";
    const std::string TOPIC_CPU   = TOPIC_BASE + "/CPUTemp";
    const std::string TOPIC_ACCEL = TOPIC_BASE + "/ADXL345";

    const int QOS = args.count("--qos") ? std::stoi(args.at("--qos")) : 1;
    const int CLEANSESSION = args.count("--cleansession")
                           ? std::stoi(args.at("--cleansession")) : 1;

    std::cout << "CPU topic:           " << TOPIC_CPU << "\n"
              << "Accelerometer topic: " << TOPIC_ACCEL << std::endl;

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    // ---- accelerometer ------------------------------------------------
    een1071::ADXL345 sensor(1);          // bus 1, default address 0x53
    if (!sensor.begin(een1071::ADXL345::Range::G4,
                      een1071::ADXL345::Resolution::Full,
                      een1071::ADXL345::DataRate::Hz100)) {
        std::cout << "Accelerometer did not initialise - check wiring, CS tied high, "
                     "and i2cdetect -y 1\n";
        return -1;
    }

    // ---- two clients, two topics --------------------------------------
    MQTTPublisher mqttCPU(ADDRESS, CLIENTID + "_cpu", USERNAME, PASSWORD, TOPIC_CPU, CLEANSESSION);
    MQTTPublisher mqttACC(ADDRESS, CLIENTID + "_accel", USERNAME, PASSWORD, TOPIC_ACCEL, CLEANSESSION);

    if (!mqttCPU.connect() || !mqttACC.connect()) {
        std::cout << "MQTT connection failed\n";
        return -1;
    }

    std::thread cpuThread(publisherThread, std::ref(mqttCPU),
                          std::function<std::string()>(makeCPUPayload),
                          QOS, std::chrono::milliseconds(1000));

    std::thread accelThread(publisherThread, std::ref(mqttACC),
                            std::function<std::string()>([&sensor]() { return makeAccelPayload(sensor); }),
                            QOS, std::chrono::milliseconds(100));

    std::cout << "Publishing. Press Ctrl-C to stop.\n";

    cpuThread.join();
    accelThread.join();

    // Explicit, so the broker sees a normal disconnect and does not send
    // the Last Will. (The destructors would also do this.)
    mqttCPU.disconnect();
    mqttACC.disconnect();

    std::cout << "Stopped cleanly\n";
    return 0;
}

// example usage
//   ./MQTTPublisherApp_withDualThreads --address tcp://<broker-host>:1883
//                                      --client "pi-publisher"
//                                      --user <username> --pword '<password>'
//                                      --topic Sensors --qos 1
