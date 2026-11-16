#pragma once

#include <string>

#include "MQTTClient.h"

// Wrapper around a Paho synchronous MQTT client, configured with a Last
// Will and Testament so the broker announces an abrupt disconnect.
//
// Owns an MQTTClient handle, so copying is forbidden and the destructor
// cleans up: forgetting disconnect() used to leak the handle, and calling
// it twice used to destroy it twice.
class MQTTPublisher {
public:
    MQTTPublisher(const std::string& addr,
                  const std::string& cid,
                  const std::string& user,
                  const std::string& pass,
                  const std::string& top,
                  int cleansession);
    ~MQTTPublisher();

    MQTTPublisher(const MQTTPublisher&) = delete;
    MQTTPublisher& operator=(const MQTTPublisher&) = delete;

    bool connect();
    bool publish(const std::string& payload, int qos);
    void disconnect();

    bool isConnected() const { return connected; }
    const std::string& getTopic() const { return topic; }

private:
    MQTTClient client = nullptr;
    MQTTClient_connectOptions opts;
    MQTTClient_willOptions will_opts;

    std::string address;
    std::string clientId;
    std::string username;
    std::string password;
    std::string topic;

    // The will options hold raw pointers into these two strings, so they
    // must outlive the connection AND be per-instance. They were
    // function-local statics before, which meant a second publisher
    // overwrote the first one's will - and could dangle its pointers if
    // the assignment reallocated.
    std::string lwt_topic;
    std::string lwt_message;

    bool connected = false;
};
