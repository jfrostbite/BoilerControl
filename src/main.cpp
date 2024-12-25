#include <ESP8266WiFi.h>
#include <PubSubClient.h> // MQTT客户端库
#include <WiFiManager.h>
#include "WebInterface.h"
#include "Config.h"

// MQTT主题定义
#define MQTT_TOPIC_CONTROL "heater/control"  // 控制主题
#define MQTT_TOPIC_STATE "heater/state"      // 状态主题
#define MQTT_TOPIC_STATUS "heater/status"    // 在线状态主题
#define MQTT_TOPIC_HEARTBEAT "heater/heartbeat"  // 心跳主题

// MQTT服务器设置
String mqtt_server = "192.168.1.5";
int mqtt_port = 1883;
String mqtt_user = "admin";
String mqtt_password = "admin";

// 继电器引脚定义
const int RELAY_PIN = 0;  // GPIO0，对应ESP-01S的IO0引脚
#define RELAY_ON  LOW   // 继电器开启时的电平
#define RELAY_OFF HIGH  // 继电器关闭时的电平

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT重连相关变量
bool mqttReconnectFailed = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000;  // 5秒重试间隔
const unsigned long RETRY_INTERVAL = 600000;    // 10分钟重试间隔
int reconnectCount = 0;
bool reconnecting = false;
unsigned long lastRetryTime = 0;

// 心跳检测相关变量
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_TIMEOUT = 90000;  // 90秒超时
bool hostOnline = false;  // Linux主机在线状态

// 创建Web界面实例
WebInterface webInterface(RELAY_PIN, &mqtt_server, &mqtt_port, &mqtt_user, client);

// 在main.cpp中添加回调处理
void onMQTTConfigChange(const String& server, const int port, const String& user, const String& pass) {
    if (client.connected()) {
        client.disconnect();
    }
    
    mqtt_password = pass;
    client.setServer(mqtt_server.c_str(), mqtt_port);
    
    // 保存新配置到EEPROM
    Config::saveMQTTConfig(server, port, user, pass);
    
    mqttReconnectFailed = false;
    reconnectCount = 0;
    reconnecting = false;
    lastReconnectAttempt = 0;
    
    webInterface.addLog("MQTT配置已更新并保存: " + server);
}

void setup_wifi() {
    
    delay(100);
    Serial.println("正在连接WiFi...");    
    WiFiManager wifiManager;
    wifiManager.autoConnect("Boiler-WiFi", "12345678");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println("等待连接WiFi...");
    }
    Serial.println("");
    Serial.println("WiFi已连接");
    Serial.println("IP地址: ");
    Serial.println(WiFi.localIP());
    
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    if (strcmp(topic, MQTT_TOPIC_CONTROL) == 0) {
        if (message == "ON" && hostOnline) {  // 只在主机在线时执行控制命令
            digitalWrite(RELAY_PIN, RELAY_ON);
            webInterface.addLog("MQTT命令：开启加热");
            // 发布新状态
            client.publish(MQTT_TOPIC_STATE, "ON", true);
        } else if (message == "OFF" && hostOnline) {  // 只在主机在线时执行控制命令
            digitalWrite(RELAY_PIN, RELAY_OFF);
            webInterface.addLog("MQTT命令：关闭加热");
            // 发布新状态
            client.publish(MQTT_TOPIC_STATE, "OFF", true);
        }
    } else if (strcmp(topic, MQTT_TOPIC_HEARTBEAT) == 0) {
        lastHeartbeat = millis();
        if (!hostOnline) {
            hostOnline = true;
            webInterface.addLog("Linux主机已上线");
        }
    }
}

void reconnect() {
    unsigned long currentMillis = millis();
    
    // 如果已经失败并且未到10分钟重试时间，直接返回
    if (mqttReconnectFailed) {
        if (currentMillis - lastRetryTime < RETRY_INTERVAL) {
            return;
        }
        // 到达重试时间，重置状态
        mqttReconnectFailed = false;
        reconnectCount = 0;
        reconnecting = false;
        webInterface.addLog("开始新一轮MQTT连接尝试");
    }
    
    // 如果正在重连中且未到重试时间，直接返回
    if (reconnecting && currentMillis - lastReconnectAttempt < RECONNECT_INTERVAL) {
        return;
    }
    
    // 开始新的重连尝试
    if (!client.connected() && !mqttReconnectFailed) {
        reconnecting = true;
        lastReconnectAttempt = currentMillis;
        
        webInterface.addLog("尝试MQTT连接...");
        String clientId = "ESP8266Client-";
        clientId += String(random(0xffff), HEX);
        
        // 设置遗嘱消息，当设备意外断开时，会发送此消息
        if (client.connect(clientId.c_str(), mqtt_user.c_str(), mqtt_password.c_str(), MQTT_TOPIC_STATUS, 0, true, "offline")) {
            reconnecting = false;
            reconnectCount = 0;
            webInterface.setMQTTError("");
            webInterface.addLog("MQTT连接成功");
            
            // 订阅主题
            client.subscribe(MQTT_TOPIC_CONTROL);
            client.subscribe(MQTT_TOPIC_HEARTBEAT);  // 订阅心跳主题
            
            // 发布在线状态
            client.publish(MQTT_TOPIC_STATUS, "online", true);
            
            // 发布当前继电器状态
            client.publish(MQTT_TOPIC_STATE, digitalRead(RELAY_PIN) == RELAY_ON ? "ON" : "OFF", true);
            
            mqttReconnectFailed = false;
        } else {
            String errorMsg;
            switch(client.state()) {
                case -4: errorMsg = "连接超时"; break;
                case -3: errorMsg = "连接丢失"; break;
                case -2: errorMsg = "连接失败"; break;
                case -1: errorMsg = "连接断开"; break;
                case 1: errorMsg = "协议版本错误"; break;
                case 2: errorMsg = "无效客户端ID"; break;
                case 3: errorMsg = "服务器不可用"; break;
                case 4: errorMsg = "用户名或密码错误"; break;
                case 5: errorMsg = "未授权"; break;
                default: errorMsg = "未知错误"; break;
            }
            webInterface.setMQTTError(errorMsg);
            webInterface.addLog("MQTT错误: " + errorMsg);
            
            if (++reconnectCount >= 3) {
                reconnecting = false;
                mqttReconnectFailed = true;
                lastRetryTime = currentMillis;  // 记录失败时间
                webInterface.addLog("MQTT连接失败次数过多，将在10分钟后重试");
                reconnectCount = 0;
            }
        }
    }
}

// 修改重置函数
void resetMQTTReconnect() {
    mqttReconnectFailed = false;
    reconnectCount = 0;
    reconnecting = false;
    lastReconnectAttempt = 0;  // 立即允许重连
    webInterface.addLog("手动触发MQTT重连");
    webInterface.setMQTTError("");  // 清除错误信息
}

void setup() {
    Serial.begin(115200);
    Config::begin();
    
    // 加载MQTT配置
    Config::loadMQTTConfig(mqtt_server, mqtt_port, mqtt_user, mqtt_password);
    
    // 先设置为输出模式，默认高电平（关闭状态）
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OFF);
    
    delay(100);  // 等待继电器状态稳定
    
    setup_wifi();
    client.setServer(mqtt_server.c_str(), mqtt_port);
    client.setCallback(mqtt_callback);
      
    webInterface.begin();
    webInterface.addLog("系统启动完成");
    webInterface.setMQTTConfigCallback(onMQTTConfigChange);
    webInterface.setMQTTReconnectCallback(resetMQTTReconnect);
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    
    if (client.connected()) {
        client.loop();
        webInterface.setMQTTStatus(true);
        
        // 检查心跳超时
        if (hostOnline && millis() - lastHeartbeat > HEARTBEAT_TIMEOUT) {
            hostOnline = false;
            webInterface.addLog("Linux主机已离线，开启加热");
            // 开启加热
            digitalWrite(RELAY_PIN, RELAY_ON);
            client.publish(MQTT_TOPIC_STATE, "ON", true);
        }
    } else {
        webInterface.setMQTTStatus(false);
    }
    
    webInterface.handle();
}