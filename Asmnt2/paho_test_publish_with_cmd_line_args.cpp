/*
 * paho_test_publish_with_cmd_line_args.cpp
 *
 * Publishes one SoC-temperature reading, with a Last Will and Testament
 * registered so the broker announces an abrupt disconnect. This is the
 * plain-C-API version kept for reference; MQTTPublisher wraps the same
 * thing in a class.
 *
 * Based on the Paho C code example from www.eclipse.org/paho/
 * (Eclipse Public License 2.0 / Eclipse Distribution License 1.0).
 */

#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <iostream>
#include <string>

#include "MQTTClient.h"
#include "MQTTHelper.h"      // parseArgs, requireArgs, readCPUTemperature

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

    // ---- payload ------------------------------------------------------
    float cpuTemp = 0.0f;
    if (!readCPUTemperature(cpuTemp)) {
        std::cout << "Could not read " << CPU_TEMP << "\n";
        return -1;
    }

    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm timeinfo{};
    char time_str[32] = {0};
    if (localtime_r(&now_time, &timeinfo) != nullptr) {
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    char str_payload[200];
    std::snprintf(str_payload, sizeof(str_payload),
                  "{\"d\":{\"CPUTemp\": %.2f, \"timestamp\": \"%s\"}}",
                  cpuTemp, time_str);

    // ---- client -------------------------------------------------------
    MQTTClient client = nullptr;
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;

    int rc = MQTTClient_create(&client, ADDRESS.c_str(), CLIENTID.c_str(),
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::printf("Failed to create client, return code %d (%s)\n",
                    rc, MQTTClient_strerror(rc));
        return -1;
    }

    opts.keepAliveInterval = 20;
    opts.cleansession = 1;
    opts.username = USERNAME.c_str();
    opts.password = PASSWORD.c_str();

    // ---- Last Will and Testament --------------------------------------
    // Separate from the normal message: different topic, different QoS,
    // different retained flag. These two strings must outlive the
    // connection, which they do - both live until the end of main.
    MQTTClient_willOptions will_opts = MQTTClient_willOptions_initializer;

    const std::string lwt_topic = TOPIC + "/HeartBeat";
    const std::string lwt_msg = CLIENTID + " Disconnected abruptly";

    will_opts.topicName = lwt_topic.c_str();
    will_opts.message = lwt_msg.c_str();
    will_opts.qos = 1;
    will_opts.retained = 1;
    opts.will = &will_opts;

    if ((rc = MQTTClient_connect(client, &opts)) != MQTTCLIENT_SUCCESS) {
        std::cout << "Failed to connect, return code " << rc
                  << " (" << MQTTClient_strerror(rc) << ")\n";
        MQTTClient_destroy(&client);
        return -1;
    }

    pubmsg.payload = str_payload;
    pubmsg.payloadlen = static_cast<int>(std::strlen(str_payload));
    pubmsg.qos = QOS;
    pubmsg.retained = 1;

    if ((rc = MQTTClient_publishMessage(client, TOPIC.c_str(), &pubmsg, &token)) != MQTTCLIENT_SUCCESS) {
        std::printf("Failed to publish, return code %d (%s)\n",
                    rc, MQTTClient_strerror(rc));
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);
        return -1;
    }

    std::cout << "Publishing: " << str_payload << " to topic " << TOPIC
              << " with ClientID: " << CLIENTID << " (QoS " << QOS << ")\n";

    rc = MQTTClient_waitForCompletion(client, token, MQTT_TIMEOUT);
    if (rc == MQTTCLIENT_SUCCESS) {
        std::cout << "Message delivered with token " << token << std::endl;
    } else {
        std::printf("Delivery not confirmed, return code %d (%s)\n",
                    rc, MQTTClient_strerror(rc));
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc == MQTTCLIENT_SUCCESS ? 0 : -1;
}

// example usage
//   ./paho_test_publish_with_cmd_line_args --address tcp://<broker-host>:1883
//                                          --client "pi-test"
//                                          --user <username> --pword '<password>'
//                                          --topic een1071/CPUTemp
