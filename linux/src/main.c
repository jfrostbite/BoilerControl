#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mosquitto.h>
#include <time.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include "aht10.h"
#include "webserver.h"
#include "logger.h"

#define MQTT_HOST "localhost"
#define MQTT_PORT 1883
#define MQTT_USER "admin"
#define MQTT_PASS "admin"

// MQTT主题定义
#define MQTT_TOPIC_CONTROL "heater/control"  // 控制主题
#define MQTT_TOPIC_STATE "heater/state"      // 状态主题
#define MQTT_TOPIC_STATUS "heater/status"    // 在线状态主题
#define MQTT_TOPIC_HEARTBEAT "heater/heartbeat"  // 心跳主题

#define I2C_BUS 0  // 使用i2c-0
#define LED_TRIGGER_PATH "/sys/class/leds/bat1/trigger"

// 全局变量
static int running = 1;
static int esp8266_online = 0;  // ESP8266在线状态
static time_t last_heartbeat = 0;  // 上次发送心跳的时间

static TempControl temp_control = {
    .day_temp_target = 21.0,    // 白天目标温度
    .night_temp_target = 20.0,  // 夜间目标温度
    .temp_hysteresis = 0.5,     // 温度滞后
    .heater_state = 0,          // 加热器状态
    .current_temp = 0.0,        // 当前温度
    .current_humidity = 0.0,    // 当前湿度
    .day_start_hour = 6,        // 早上6点开始
    .night_start_hour = 22      // 晚上10点结束
};

// 函数声明
const char* get_current_time(void);
void print_local_ip(void);
void signal_handler(int signo);
void setup_logging();
void cleanup_logging();

// 信号处理函数
void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        running = 0;
    }
}

// 获取当前时间的函数
const char* get_current_time(void) {
    static char buffer[26];
    time_t timer;
    struct tm* tm_info;

    time(&timer);
    tm_info = localtime(&timer);
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

// 获取本地IP地址的函数
void print_local_ip(void) {
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;

    getifaddrs(&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            if (strcmp(addr, "127.0.0.1") != 0) {
                logger_log(LOG_LEVEL_INFO, "本地IP地址: %s", addr);
            }
        }
    }
    freeifaddrs(ifap);
}

// MQTT回调函数
void mqtt_connect_callback(struct mosquitto *mosq, void *obj, int result) {
    if (result == 0) {
        logger_log(LOG_LEVEL_INFO, "MQTT连接成功");
        print_local_ip();
    } else {
        logger_log(LOG_LEVEL_ERROR, "MQTT连接失败，错误码：%d", result);
    }
}

// 添加消息发布回调
void mqtt_publish_callback(struct mosquitto *mosq, void *obj, int mid) {
    logger_log(LOG_LEVEL_INFO, "MQTT消息发布成功，消息ID：%d", mid);
}

// 控制LED的函数
void control_led(int on) {
    FILE *fp = fopen(LED_TRIGGER_PATH, "w");
    if (fp) {
        fprintf(fp, "%s", on ? "heartbeat" : "none");
        fclose(fp);
    } else {
        logger_log(LOG_LEVEL_ERROR, "无法控制LED: %s", strerror(errno));
    }
}

// MQTT消息回调函数
void mqtt_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message) {
    if (strcmp(message->topic, MQTT_TOPIC_STATE) == 0) {
        // 更新加热器实际状态
        if (strncmp(message->payload, "ON", 2) == 0) {
            temp_control.heater_state = 1;
            control_led(1);  // 开启LED
            logger_log(LOG_LEVEL_INFO, "ESP8266报告加热器已开启");
        } else if (strncmp(message->payload, "OFF", 3) == 0) {
            temp_control.heater_state = 0;
            control_led(0);  // 关闭LED
            logger_log(LOG_LEVEL_INFO, "ESP8266报告加热器已关闭");
        }
    } else if (strcmp(message->topic, MQTT_TOPIC_STATUS) == 0) {
        // 更新ESP8266在线状态
        if (strncmp(message->payload, "online", 6) == 0) {
            esp8266_online = 1;
            logger_log(LOG_LEVEL_INFO, "ESP8266已上线");
        } else if (strncmp(message->payload, "offline", 7) == 0) {
            esp8266_online = 0;
            logger_log(LOG_LEVEL_INFO, "ESP8266已离线");
        }
    }
}

// 获取当前小时
static int get_current_hour() {
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    return timeinfo->tm_hour;
}

// 获取当前目标温度
static float get_current_target_temp(TempControl *ctrl) {
    int current_hour = get_current_hour();
    if (current_hour >= ctrl->day_start_hour && current_hour < ctrl->night_start_hour) {
        return ctrl->day_temp_target;
    } else {
        return ctrl->night_temp_target;
    }
}

// 在主循环中使用新的目标温度获取函数
void temp_control_loop(TempControl *ctrl, struct mosquitto *mosq) {
    float target_temp = get_current_target_temp(ctrl);
    int rc;
    
    // 如果当前温度低于目标温度减去滞后值，开启加热
    if (ctrl->current_temp < target_temp - ctrl->temp_hysteresis) {
        if (!ctrl->heater_state) {
            rc = mosquitto_publish(mosq, NULL, MQTT_TOPIC_CONTROL, 2, "ON", 0, false);
            if (rc != MOSQ_ERR_SUCCESS) {
                logger_log(LOG_LEVEL_ERROR, "MQTT发布失败: %s", mosquitto_strerror(rc));
            }
            ctrl->heater_state = 1;
            add_log("加热器开启：当前温度 %.1f°C < 目标温度 %.1f°C - %.1f°C", 
                   ctrl->current_temp, target_temp, ctrl->temp_hysteresis);
        }
    }
    // 如果当前温度高于目标温度，关闭加热
    else if (ctrl->current_temp > target_temp) {
        if (ctrl->heater_state) {
            rc = mosquitto_publish(mosq, NULL, MQTT_TOPIC_CONTROL, 3, "OFF", 0, false);
            if (rc != MOSQ_ERR_SUCCESS) {
                logger_log(LOG_LEVEL_ERROR, "MQTT发布失败: %s", mosquitto_strerror(rc));
            }
            ctrl->heater_state = 0;
            add_log("加热器关闭：当前温度 %.1f°C > 目标温度 %.1f°C", 
                   ctrl->current_temp, target_temp);
        }
    }
}

int main(int argc, char *argv[]) {
    int i2c_fd;
    struct mosquitto *mosq;
    int rc;

    // 初始化日志系统
    logger_init("temp_control");
    logger_log(LOG_LEVEL_INFO, "程序启动");

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化AHT10
    i2c_fd = aht10_init(I2C_BUS);
    if (i2c_fd < 0) {
        logger_log(LOG_LEVEL_ERROR, "AHT10初始化失败");
        return 1;
    }

    // 初始化MQTT
    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        logger_log(LOG_LEVEL_ERROR, "MQTT初始化失败");
        aht10_close(i2c_fd);
        return 1;
    }

    // 设置MQTT回调
    mosquitto_connect_callback_set(mosq, mqtt_connect_callback);
    mosquitto_publish_callback_set(mosq, mqtt_publish_callback);
    mosquitto_message_callback_set(mosq, mqtt_message_callback);
    mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASS);

    logger_log(LOG_LEVEL_INFO, "正在连接MQTT服务器 %s:%d...", MQTT_HOST, MQTT_PORT);
    rc = mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_log(LOG_LEVEL_ERROR, "MQTT连接失败: %s", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        aht10_close(i2c_fd);
        return 1;
    }

    // 订阅主题
    rc = mosquitto_subscribe(mosq, NULL, MQTT_TOPIC_STATE, 0);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_log(LOG_LEVEL_ERROR, "MQTT订阅失败: %s", mosquitto_strerror(rc));
    }
    
    rc = mosquitto_subscribe(mosq, NULL, MQTT_TOPIC_STATUS, 0);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_log(LOG_LEVEL_ERROR, "MQTT订阅失败: %s", mosquitto_strerror(rc));
    }

    rc = mosquitto_loop_start(mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_log(LOG_LEVEL_ERROR, "MQTT循环启动失败: %s", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        aht10_close(i2c_fd);
        return 1;
    }

    // 初始化时关闭LED
    control_led(0);

    // 启动Web服务器
    if (start_webserver(&temp_control) != 0) {
        logger_log(LOG_LEVEL_ERROR, "Web服务器启动失败");
        mosquitto_loop_stop(mosq, true);
        mosquitto_destroy(mosq);
        aht10_close(i2c_fd);
        return 1;
    }

    // 主循环
    while (running) {
        time_t current_time = time(NULL);
        
        // 每30秒发送一次心跳包
        if (current_time - last_heartbeat >= 30) {
            rc = mosquitto_publish(mosq, NULL, MQTT_TOPIC_HEARTBEAT, 2, "ping", 0, false);
            if (rc != MOSQ_ERR_SUCCESS) {
                logger_log(LOG_LEVEL_ERROR, "心跳包发送失败: %s", mosquitto_strerror(rc));
            }
            last_heartbeat = current_time;
        }

        if (aht10_read_sensor(i2c_fd, &temp_control.current_temp, &temp_control.current_humidity) == 0) {
            logger_log(LOG_LEVEL_INFO, "温度: %.1f°C, 湿度: %.1f%%, 加热器当前状态: %s", 
                   temp_control.current_temp, temp_control.current_humidity,
                   temp_control.heater_state ? "开启" : "关闭");

            // 只在ESP8266在线时执行温控逻辑
            if (esp8266_online) {
                // 温度控制逻辑
                temp_control_loop(&temp_control, mosq);
            } else {
                logger_log(LOG_LEVEL_INFO, "ESP8266离线，等待设备重新连接...");
            }
        } else {
            logger_log(LOG_LEVEL_ERROR, "读取传感器失败");
        }

        sleep(30);
    }

    // 程序结束时关闭LED
    control_led(0);

    // 清理
    logger_log(LOG_LEVEL_INFO, "程序结束");
    stop_webserver();
    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    aht10_close(i2c_fd);
    logger_cleanup();

    return 0;
} 