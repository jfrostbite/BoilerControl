#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <vector>

#define MAX_LOGS 50  // 最多保存50条日志

struct LogEntry {
    String timestamp;
    String message;
};

class WebInterface {
public:
    WebInterface(int relayPin, String* mqtt_server, int* mqtt_port, String* mqtt_user, PubSubClient& mqttClient)
        : client(mqttClient)
        , server(80)
        , relayPin(relayPin)
        , mqtt_server_ptr(mqtt_server)
        , mqtt_port_ptr(mqtt_port)
        , mqtt_user_ptr(mqtt_user)
        , mqttConnected(false)
        , mqttConfigCallback(nullptr)
        , mqttReconnectCallback(nullptr)
    {
        // 构造函数体
    }

    void begin();
    void handle();
    void addLog(const String& message);
    void setMQTTStatus(bool connected);
    void setMQTTError(const String& error) { mqttErrorMsg = error; }
    void setMQTTConfigCallback(void (*callback)(const String&, const int, const String&, const String&)) {
        mqttConfigCallback = callback;
    }
    void setMQTTReconnectCallback(void (*callback)()) {
        mqttReconnectCallback = callback;
    }

private:
    PubSubClient& client;
    ESP8266WebServer server;
    int relayPin;
    String* mqtt_server_ptr;
    int* mqtt_port_ptr;
    String* mqtt_user_ptr;
    bool mqttConnected;
    String mqttErrorMsg;
    std::vector<LogEntry> logs;
    void (*mqttConfigCallback)(const String&, const int, const String&, const String&);
    void (*mqttReconnectCallback)();

    String getTimestamp();
    String getHTML();
    void handleRoot();
    void handleStatus();
    void handleLogs();
    void handleToggle();
    void handleMQTTSave();
    void handleMQTTReconnect();
};

#endif 