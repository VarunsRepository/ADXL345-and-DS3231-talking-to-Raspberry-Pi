/*
 * MQTT_SubscriberApp.cpp
 *
 * Subscribes to a topic and drives three LEDs from the payload. Accepts
 * either the accelerometer JSON published by MQTTPublisherApp
 * ({"d":{"pitch":..,"roll":..,"accel":{..}}}) or a plain colour word
 * (BLUE / GREEN / RED) for testing without the publisher running.
 *
 * Replaces the four near-identical subscriber files.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include "MQTTClient.h"
#include "MQTTHelper.h"
#include "../GPIOManager/GPIOManager.h"

using json = nlohmann::json;

#define QOS 1

namespace {

// Pitch outside this band counts as "tilted".
constexpr double kPitchThresholdDegrees = 20.0;

// Everything the message handler needs. It is passed to Paho as the
// callback context, which is how data gets out of a callback that cannot
// return a value.
//
// No mutex here: Paho's synchronous client delivers messages from a single
// background thread and never runs two callbacks at once, and main() does
// not touch the LEDs once setup is done. The previous design polled a
// shared buffer from main while the callback wrote it, which was a data
// race; doing the work inside the callback removes the shared state
// entirely.
struct Handler {
    GPIOManager* leds = nullptr;
    unsigned int blue = 0;
    unsigned int green = 0;
    unsigned int red = 0;
};

void showColour(const Handler& h, bool blue, bool green, bool red)
{
    h.leds->writePin(h.blue, blue);
    h.leds->writePin(h.green, green);
    h.leds->writePin(h.red, red);
}

void handlePayload(const Handler& h, const std::string& payload)
{
    // Preferred form: the accelerometer JSON.
    try {
        const json parsed = json::parse(payload);
        const double pitch = parsed.at("d").at("pitch").get<double>();

        std::printf("Pitch: %.2f\n", pitch);

        if (pitch < -kPitchThresholdDegrees) {
            showColour(h, true, false, false);      // tilted one way
        } else if (pitch < kPitchThresholdDegrees) {
            showColour(h, false, true, false);      // roughly level
        } else {
            showColour(h, false, false, true);      // tilted the other way
        }
        return;
    } catch (const std::exception& e) {
        // Not JSON, or no "d"/"pitch" in it - fall through to the plain
        // colour commands rather than dropping the message silently.
        std::printf("Not accelerometer JSON (%s), trying colour command\n", e.what());
    }

    if (payload == "BLUE") {
        showColour(h, true, false, false);
    } else if (payload == "GREEN") {
        showColour(h, false, true, false);
    } else if (payload == "RED") {
        showColour(h, false, false, true);
    } else {
        std::printf("Unknown command: %s\n", payload.c_str());
    }
}

int msgarrvd(void* context, char* topicName, int topicLen, MQTTClient_message* message)
{
    (void)topicLen;

    Handler* handler = static_cast<Handler*>(context);

    // message->payload is not NUL-terminated, so the length is used rather
    // than treating it as a C string.
    const std::string payload(static_cast<const char*>(message->payload),
                              static_cast<std::size_t>(message->payloadlen));

    std::printf("\nMessage arrived\n     topic: %s\n   message: %s\n",
                topicName, payload.c_str());

    if (handler && handler->leds) {
        handlePayload(*handler, payload);
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;                     // 1 = message consumed successfully
}

void connlost(void* context, char* cause)
{
    (void)context;
    std::printf("\nConnection lost\n     cause: %s\n", cause ? cause : "unknown");
}

void delivered(void* context, MQTTClient_deliveryToken dt)
{
    (void)context;
    std::printf("Message with token value %d delivery confirmed\n", dt);
}

} /* anonymous namespace */

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

    const int CLEANSESSION = args.count("--cleansession")
                           ? std::stoi(args.at("--cleansession")) : 1;

    // ---- LEDs ---------------------------------------------------------
    const unsigned int blue_LED  = 17;
    const unsigned int green_LED = 27;
    const unsigned int red_LED   = 22;

    GPIOManager* ledsPtr = nullptr;
    try {
        static GPIOManager leds("/dev/gpiochip0", "een1071-subscriber");
        leds.configureOutputs({blue_LED, green_LED, red_LED});

        // All three off. This used to pass `true`, which on active-high
        // wiring turned them all on despite the comment saying otherwise.
        leds.writePin(blue_LED, false);
        leds.writePin(green_LED, false);
        leds.writePin(red_LED, false);

        ledsPtr = &leds;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "GPIO setup failed: %s\n", e.what());
        return -1;
    }

    Handler handler;
    handler.leds  = ledsPtr;
    handler.blue  = blue_LED;
    handler.green = green_LED;
    handler.red   = red_LED;

    // ---- MQTT ---------------------------------------------------------
    MQTTClient client = nullptr;
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;

    int rc = MQTTClient_create(&client, ADDRESS.c_str(), CLIENTID.c_str(),
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::fprintf(stderr, "Failed to create client, return code %d (%s)\n",
                     rc, MQTTClient_strerror(rc));
        return -1;
    }

    opts.keepAliveInterval = 20;
    opts.cleansession = CLEANSESSION;
    opts.username = USERNAME.c_str();
    opts.password = PASSWORD.c_str();

    MQTTClient_setCallbacks(client, &handler, connlost, msgarrvd, delivered);

    if ((rc = MQTTClient_connect(client, &opts)) != MQTTCLIENT_SUCCESS) {
        std::fprintf(stderr, "Failed to connect, return code %d (%s)\n",
                     rc, MQTTClient_strerror(rc));
        MQTTClient_destroy(&client);
        return -1;
    }

    // .c_str() on both: passing a std::string to a %s is undefined
    // behaviour, which is what the previous version did with CLIENTID.
    std::printf("Subscribing to topic %s\nfor client %s using QoS%d\n\n"
                "Press Q<Enter> to quit\n\n",
                TOPIC.c_str(), CLIENTID.c_str(), QOS);

    if ((rc = MQTTClient_subscribe(client, TOPIC.c_str(), QOS)) != MQTTCLIENT_SUCCESS) {
        std::fprintf(stderr, "Failed to subscribe, return code %d (%s)\n",
                     rc, MQTTClient_strerror(rc));
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);
        return -1;
    }

    // Messages are handled on Paho's thread, so main just waits for the
    // quit key. This restores the Q<Enter> path the banner advertises and
    // lets us disconnect cleanly - which matters here, because an abrupt
    // exit fires our own Last Will message.
    int ch = 0;
    do {
        ch = std::getchar();
    } while (ch != 'Q' && ch != 'q' && ch != EOF);

    MQTTClient_unsubscribe(client, TOPIC.c_str());
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);

    std::printf("Disconnected cleanly\n");
    return 0;
}

// example usage
//   ./MQTT_SubscriberApp --address tcp://<broker-host>:1883
//                        --topic Sensors/ADXL345
//                        --client "pi-subscriber"
//                        --user <username> --pword '<password>'
