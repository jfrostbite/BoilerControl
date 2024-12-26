#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <microhttpd.h>
#include <json-c/json.h>

// Web服务器配置
#define WEB_PORT 8080
#define CONFIG_FILE "./temp_control.conf"
#define MAX_LOGS 100  // 最多保存100条日志

// 日志结构体
typedef struct {
    char timestamp[32];  // 时间戳
    char message[256];   // 日志消息
} LogEntry;

// 温控器配置结构体
typedef struct {
    float day_temp_target;   // 白天目标温度
    float night_temp_target; // 夜间目标温度
    float temp_hysteresis;   // 温度滞后
    int heater_state;       // 加热器状态
    float current_temp;     // 当前温度
    float current_humidity; // 当前湿度
    int day_start_hour;     // 白天开始时间（小时）
    int night_start_hour;   // 夜间开始时间（小时）
} TempControl;

// 函数声明
int start_webserver(TempControl *ctrl);
void stop_webserver(void);
void update_sensor_data(float temp, float humidity);
int save_config(const TempControl *ctrl);
int load_config(TempControl *ctrl);
void add_log(const char *format, ...);  // 添加日志的函数

#endif 