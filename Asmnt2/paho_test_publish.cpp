/*
 * paho_test_publish.cpp
 *
 * Minimal single publish, used to prove the broker connection works.
 * Based on the Paho C code example from www.eclipse.org/paho/
 * (Eclipse Public License 2.0 / Eclipse Distribution License 1.0).
 *
 * Everything comes from argv: no credentials in the source.
 */

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "MQTTClient.h"

#define CPU_TEMP_PATH "/sys/class/thermal/thermal_zone0/temp"
#define QOS 1
#define TIMEOUT 10000L

// Reads the SoC temperature in degrees Celsius; false if sysfs is unreadable.
static bool getCPUTemperature(float& celsius)
{
    std::ifstream fs(CPU_TEMP_PATH);
    if (!fs) {
        return false;
    }

    int milliDegrees = 0;
    fs >> milliDegrees;
    if (!fs) {
        return false;
    }

    celsius = static_cast<float>(milliDegrees) / 1000.0f;
    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 6) {
        std::cout << "Usage: " << argv[0]
                  << " <address> <clientid> <username> <password> <topic>\n"
                  << "   eg: " << argv[0]
                  << " tcp://<broker-host>:1883 pi-test <username> '<password>'"
                     " een1071/CPUTemp\n";
        return -1;
    }

    // These used to be both #define'd and declared as variables, which made
    // the preprocessor rewrite the declarations into string literals and the
    // file would not compile. The macros are gone; argv is the only source.
    const char* address  = argv[1];
    const char* clientId = argv[2];
    const char* username = argv[3];
    const char* password = argv[4];
    const char* topic    = argv[5];

    float cpuTemp = 0.0f;
    if (!getCPUTemperature(cpuTemp)) {
        std::cout << "Could not read " << CPU_TEMP_PATH << "\n";
        return -1;
    }

    char str_payload[128];
    std::snprintf(str_payload, sizeof(str_payload),
                  "{\"d\":{\"CPUTemp\": %.2f}}", cpuTemp);

    MQTTClient client = nullptr;
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;

    int rc = MQTTClient_create(&client, address, clientId,
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::printf("Failed to create client, return code %d (%s)\n",
                    rc, MQTTClient_strerror(rc));
        return -1;
    }

    opts.keepAliveInterval = 20;
    opts.cleansession = 1;
    opts.username = username;
    opts.password = password;

    if ((rc = MQTTClient_connect(client, &opts)) != MQTTCLIENT_SUCCESS) {
        std::printf("Failed to connect, return code %d (%s)\n",
                    rc, MQTTClient_strerror(rc));
        MQTTClient_destroy(&client);
        return -1;
    }

    pubmsg.payload = str_payload;
    pubmsg.payloadlen = static_cast<int>(std::strlen(str_payload));
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    if ((rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS) {
        std::printf("Failed to publish, return code %d (%s)\n",
                    rc, MQTTClient_strerror(rc));
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);
        return -1;
    }

    std::cout << "Waiting up to " << static_cast<int>(TIMEOUT / 1000)
              << "s for publication of " << str_payload
              << "\non topic " << topic << " for client " << clientId << std::endl;

    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    if (rc == MQTTCLIENT_SUCCESS) {
        std::cout << "Message with token " << static_cast<int>(token) << " delivered." << std::endl;
    } else {
        std::printf("Delivery not confirmed, return code %d (%s)\n",
                    rc, MQTTClient_strerror(rc));
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc == MQTTCLIENT_SUCCESS ? 0 : -1;
}
