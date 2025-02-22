#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <microhttpd.h>
#include <json-c/json.h>

// Web服务器配置
#define WEB_PORT 8080
#define CONFIG_DIR "~/.config/temp_control"  // 配置目录
#define CONFIG_FILE CONFIG_DIR "/config.json"  // 配置文件
#define DATA_DIR CONFIG_DIR "/data"  // 数据存储目录
#define MAX_LOGS 100  // 最多保存100条日志
#define DATA_RETENTION_DAYS 30  // 数据保留天数

// 温度数据点结构体
typedef struct {
    char timestamp[32];  // 时间戳
    float temperature;   // 温度
    int heater_state;   // 加热器状态
} TempDataPoint;

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
void save_temp_data(float temp, int heater_state);  // 保存温度数据
char* get_today_data(void);  // 获取当天的温度数据
int init_config_dir(void);  // 新增函数声明
void cleanup_old_data(void);  // 清理过期数据

#endif 