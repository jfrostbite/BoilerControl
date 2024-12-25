#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <EEPROM.h>

struct MQTTConfig {
    char server[40];
    int port;
    char user[20];
    char password[20];
    uint8_t valid;  // 用于检查EEPROM是否已初始化
};

class Config {
public:
    static void begin() {
        EEPROM.begin(512);
    }
    
    static void loadMQTTConfig(String& server, int& port, String& user, String& password) {
        MQTTConfig config;
        EEPROM.get(0, config);
        
        // 检查配置是否有效
        if (config.valid == 0x55) {
            server = String(config.server);
            port = config.port;
            user = String(config.user);
            password = String(config.password);
        }
    }
    
    static void saveMQTTConfig(const String& server, int port, const String& user, const String& password) {
        MQTTConfig config;
        server.toCharArray(config.server, sizeof(config.server));
        config.port = port;
        user.toCharArray(config.user, sizeof(config.user));
        password.toCharArray(config.password, sizeof(config.password));
        config.valid = 0x55;  // ���记配置为有效
        
        EEPROM.put(0, config);
        EEPROM.commit();
    }
};

#endif 