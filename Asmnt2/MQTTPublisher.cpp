#include "MQTTPublisher.h"

#include <iostream>

#include "MQTTHelper.h"

MQTTPublisher::MQTTPublisher(const std::string& addr,
                             const std::string& cid,
                             const std::string& user,
                             const std::string& pass,
                             const std::string& top,
                             int cleansession)
    : address(addr), clientId(cid), username(user), password(pass), topic(top)
{
    MQTTClient_create(&client,
                      address.c_str(),
                      clientId.c_str(),
                      MQTTCLIENT_PERSISTENCE_NONE, // nothing stored locally if the link breaks
                      nullptr);

    opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = 20;
    opts.cleansession = cleansession;
    opts.username = username.c_str();
    opts.password = password.c_str();

    // ---- Last Will and Testament -------------------------------------
    will_opts = MQTTClient_willOptions_initializer;

    lwt_topic   = topic + "/HeartBeat";
    lwt_message = clientId + " Disconnected abruptly";

    // Safe because lwt_topic and lwt_message are members: they live as
    // long as will_opts, and each publisher has its own pair.
    will_opts.topicName = lwt_topic.c_str();
    will_opts.message   = lwt_message.c_str();
    will_opts.qos       = 1;
    will_opts.retained  = 1;

    opts.will = &will_opts;
}

MQTTPublisher::~MQTTPublisher()
{
    disconnect();
}

bool MQTTPublisher::connect()
{
    if (!client) {
        std::cout << "Connection failed: client was never created\n";
        return false;
    }
    if (connected) {
        return true;
    }

    const int rc = MQTTClient_connect(client, &opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cout << "Connection failed: " << rc
                  << " (" << MQTTClient_strerror(rc) << ")\n";
        return false;
    }

    connected = true;
    return true;
}

bool MQTTPublisher::publish(const std::string& payload, int qos)
{
    if (!connected) {
        std::cout << "Publish skipped: not connected\n";
        return false;
    }

    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;

    pubmsg.payload    = const_cast<char*>(payload.c_str());
    pubmsg.payloadlen = static_cast<int>(payload.length());
    pubmsg.qos        = qos;
    pubmsg.retained   = 1;

    int rc = MQTTClient_publishMessage(client, topic.c_str(), &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cout << "Publish failed: " << rc
                  << " (" << MQTTClient_strerror(rc) << ")\n";
        return false;
    }

    rc = MQTTClient_waitForCompletion(client, token, MQTT_TIMEOUT);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cout << "Delivery not confirmed: " << rc
                  << " (" << MQTTClient_strerror(rc) << ")\n";
        return false;
    }

    std::cout << "Message delivered to " << topic << " with token " << token << std::endl;
    return true;
}

void MQTTPublisher::disconnect()
{
    if (!client) {
        return;                      // already destroyed: nothing to do
    }

    if (connected) {
        MQTTClient_disconnect(client, 10000);
        connected = false;
    }

    MQTTClient_destroy(&client);
    client = nullptr;                // so a second call is a no-op
}
