#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <ESP8266WebServer.h>
#include <vector>
#include <functional>
#include <PubSubClient.h>

struct LogEntry {
    String timestamp;
    String message;
};

class WebInterface {
public:
    WebInterface(int relayPin, String* mqtt_server, int* mqtt_port, String* mqtt_user, PubSubClient& mqttClient)
        : server(80), relayPin(relayPin), mqttConnected(false),
          mqtt_server_ptr(mqtt_server), mqtt_port_ptr(mqtt_port), mqtt_user_ptr(mqtt_user), client(mqttClient) {}
    void begin();
    void handle();
    void addLog(const String& message);
    void setMQTTStatus(bool connected);
    typedef std::function<void(const String&, const int, const String&, const String&)> MQTTConfigCallback;
    void setMQTTConfigCallback(MQTTConfigCallback cb) { mqttConfigCallback = cb; }
    typedef std::function<void()> MQTTReconnectCallback;
    void setMQTTReconnectCallback(MQTTReconnectCallback cb) { mqttReconnectCallback = cb; }
    void setMQTTError(const String& error) { mqttErrorMsg = error; }
    void updateMQTTConfig(const String& server, const int port, const String& user) {
        *mqtt_server_ptr = server;
        *mqtt_port_ptr = port;
        *mqtt_user_ptr = user;
    }

private:
    ESP8266WebServer server;
    PubSubClient& client;
    std::vector<LogEntry> logs;
    int relayPin;
    bool mqttConnected;
    static const size_t MAX_LOGS = 50;
    MQTTConfigCallback mqttConfigCallback;
    MQTTReconnectCallback mqttReconnectCallback;
    String* mqtt_server_ptr;
    int* mqtt_port_ptr;
    String* mqtt_user_ptr;
    String mqttErrorMsg;

    void handleRoot();
    void handleStatus();
    void handleLogs();
    void handleToggle();
    String getTimestamp();
    String getHTML();
    void handleMQTTConfig();
    void handleMQTTSave();
    void handleMQTTReconnect();
};

#endif 