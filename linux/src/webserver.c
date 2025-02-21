#define _XOPEN_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <stdbool.h>
#include <dirent.h>     // 用于目录操作
#include <limits.h>     // 用于 PATH_MAX
#include "webserver.h"
#include "logger.h"
#include "index_html.h"
#include "database.h"
#include "utils.h"

static struct MHD_Daemon *httpd;
static TempControl *temp_control;
static LogEntry logs[MAX_LOGS];  // 日志数组
static int log_count = 0;        // 当前日志数量

// 初始化配置目录
int init_config_dir(void) {
    char* config_path = expand_path(CONFIG_DIR);
    char* data_path = expand_path(DATA_DIR);
    
    if (!config_path || !data_path) {
        free(config_path);
        free(data_path);
        return -1;
    }

    // 创建配置目录
    struct stat st = {0};
    if (stat(config_path, &st) == -1) {
        if (mkdir(config_path, 0755) == -1) {
            free(config_path);
            free(data_path);
            return -1;
        }
    }

    // 创建数据目录
    if (stat(data_path, &st) == -1) {
        if (mkdir(data_path, 0755) == -1) {
            free(config_path);
            free(data_path);
            return -1;
        }
    }

    free(config_path);
    free(data_path);
    return 0;
}

// 获取当前时间字符串
static void get_timestamp(char *buffer, size_t size) {
    time_t now;
    struct tm *timeinfo;
    
    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
}

// 添加日志
void add_log(const char *format, ...) {
    va_list args;
    char message[256];
    
    // 格式化消息
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // 如果日志满了，移除最旧的日志
    if (log_count >= MAX_LOGS) {
        memmove(&logs[0], &logs[1], sizeof(LogEntry) * (MAX_LOGS - 1));
        log_count = MAX_LOGS - 1;
    }
    
    // 添加新日志
    get_timestamp(logs[log_count].timestamp, sizeof(logs[log_count].timestamp));
    
    // 使用 snprintf 替代 strncpy
    snprintf(logs[log_count].message, sizeof(logs[log_count].message), "%s", message);
    
    log_count++;
}

// 保存配置到文件
int save_config(const TempControl *ctrl) {
    char* config_path = expand_path(CONFIG_FILE);
    if (!config_path) {
        printf("展开配置文件路径失败\n");
        return -1;
    }

    json_object *json = json_object_new_object();
    json_object_object_add(json, "day_temp_target", json_object_new_double(ctrl->day_temp_target));
    json_object_object_add(json, "night_temp_target", json_object_new_double(ctrl->night_temp_target));
    json_object_object_add(json, "hysteresis", json_object_new_double(ctrl->temp_hysteresis));
    json_object_object_add(json, "day_start_hour", json_object_new_int(ctrl->day_start_hour));
    json_object_object_add(json, "night_start_hour", json_object_new_int(ctrl->night_start_hour));
    
    const char *json_str = json_object_to_json_string(json);
    FILE *fp = fopen(config_path, "w");
    if (!fp) {
        printf("保存配置失败: %s\n", strerror(errno));
        json_object_put(json);
        free(config_path);
        return -1;
    }
    
    fprintf(fp, "%s\n", json_str);
    fclose(fp);
    json_object_put(json);
    printf("配置已保存到 %s\n", config_path);
    free(config_path);
    return 0;
}

// 从文件加载配置
int load_config(TempControl *ctrl) {
    char* config_path = expand_path(CONFIG_FILE);
    if (!config_path) {
        printf("展开配置文件路径失败\n");
        return -1;
    }

    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        printf("加载配置失败: %s\n", strerror(errno));
        printf("使用默认配置\n");
        // 使用默认值
        ctrl->day_temp_target = 21.0;
        ctrl->night_temp_target = 20.0;
        ctrl->temp_hysteresis = 0.5;
        ctrl->day_start_hour = 6;    // 早上6点
        ctrl->night_start_hour = 22; // 晚上10点
        free(config_path);
        // 尝试创建配置文件
        save_config(ctrl);
        return 0;  // 不将其视为错误
    }
    
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), fp)) {
        json_object *json = json_tokener_parse(buffer);
        if (json) {
            json_object *obj;
            if (json_object_object_get_ex(json, "day_temp_target", &obj)) {
                ctrl->day_temp_target = json_object_get_double(obj);
            }
            if (json_object_object_get_ex(json, "night_temp_target", &obj)) {
                ctrl->night_temp_target = json_object_get_double(obj);
            }
            if (json_object_object_get_ex(json, "hysteresis", &obj)) {
                ctrl->temp_hysteresis = json_object_get_double(obj);
            }
            if (json_object_object_get_ex(json, "day_start_hour", &obj)) {
                ctrl->day_start_hour = json_object_get_int(obj);
            }
            if (json_object_object_get_ex(json, "night_start_hour", &obj)) {
                ctrl->night_start_hour = json_object_get_int(obj);
            }
            json_object_put(json);
        }
    }
    
    fclose(fp);
    free(config_path);
    return 0;
}

// 保存温度数据
void save_temp_data(float temp, int heater_state) {
    db_save_temp_data(temp, temp_control->current_humidity, heater_state);
}

// 获取今天的温度数据
char* get_today_data(void) {
    char today[11];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(today, sizeof(today), "%Y-%m-%d", tm_info);
    return db_get_temp_data(today);
}

// 处理GET请求的回调函数
static enum MHD_Result handle_get_request(void *cls, struct MHD_Connection *connection,
                            const char *url, const char *method,
                            const char *version, const char *upload_data,
                            size_t *upload_data_size, void **con_cls) {
    struct MHD_Response *response;
    enum MHD_Result ret;
    
    if (strcmp(url, "/") == 0) {
        // 返回HTML页面
        response = MHD_create_response_from_buffer(strlen(HTML_PAGE),
                                                 (void*)HTML_PAGE,
                                                 MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
    } else if (strcmp(url, "/api/status") == 0) {
        // 创建JSON响应
        json_object *json = json_object_new_object();
        json_object_object_add(json, "current_temp", json_object_new_double(temp_control->current_temp));
        json_object_object_add(json, "current_humidity", json_object_new_double(temp_control->current_humidity));
        json_object_object_add(json, "day_temp_target", json_object_new_double(temp_control->day_temp_target));
        json_object_object_add(json, "night_temp_target", json_object_new_double(temp_control->night_temp_target));
        json_object_object_add(json, "hysteresis", json_object_new_double(temp_control->temp_hysteresis));
        json_object_object_add(json, "heater_state", json_object_new_boolean(temp_control->heater_state));
        
        const char *json_str = json_object_to_json_string(json);
        response = MHD_create_response_from_buffer(strlen(json_str),
                                                 (void*)json_str,
                                                 MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        json_object_put(json);
    } else if (strcmp(url, "/api/logs") == 0) {
        // 创建日志JSON响应
        json_object *json_array = json_object_new_array();
        for (int i = 0; i < log_count; i++) {
            json_object *log_obj = json_object_new_object();
            json_object_object_add(log_obj, "time", json_object_new_string(logs[i].timestamp));
            json_object_object_add(log_obj, "msg", json_object_new_string(logs[i].message));
            json_object_array_add(json_array, log_obj);
        }
        
        const char *json_str = json_object_to_json_string(json_array);
        response = MHD_create_response_from_buffer(strlen(json_str),
                                                 (void*)json_str,
                                                 MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        json_object_put(json_array);
    } else if (strncmp(url, "/api/temp_data", 13) == 0) {
        const char* date_param = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "date");
        char* data;
        if (date_param) {
            // 如果提供了日期参数，使用指定日期
            data = db_get_temp_data(date_param);
        } else {
            // 否则使用今天的日期
            char today[11];
            time_t now = time(NULL);
            strftime(today, sizeof(today), "%Y-%m-%d", localtime(&now));
            data = db_get_temp_data(today);
        }
        
        if (!data) {
            data = strdup("{\"data\":[]}");
        }
        
        response = MHD_create_response_from_buffer(strlen(data),
                                                 (void*)data,
                                                 MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
    } else {
        const char *not_found = "404 Not Found";
        response = MHD_create_response_from_buffer(strlen(not_found),
                                                 (void*)not_found,
                                                 MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "text/plain");
        return MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    }
    
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

// 处理POST请求的回调函数
static enum MHD_Result handle_post_request(void *cls, struct MHD_Connection *connection,
                             const char *url, const char *method,
                             const char *version, const char *upload_data,
                             size_t *upload_data_size, void **con_cls) {
    static int dummy;
    struct MHD_Response *response;
    enum MHD_Result ret;
    json_object *response_json = NULL;
    static char buffer[1024];
    static size_t buffer_pos = 0;

    if (*con_cls == NULL) {
        *con_cls = &dummy;
        buffer_pos = 0;
        return MHD_YES;
    }

    if (*upload_data_size != 0) {
        // 处理上传的数据
        if (buffer_pos + *upload_data_size > sizeof(buffer) - 1)
            return MHD_NO;
        
        memcpy(buffer + buffer_pos, upload_data, *upload_data_size);
        buffer_pos += *upload_data_size;
        *upload_data_size = 0;
        return MHD_YES;
    }

    // 确保数据以null结尾
    buffer[buffer_pos] = '\0';
    
    // 根据URL路由处理不同的POST请求
    if (strcmp(url, "/api/settings") == 0) {
        // 解析JSON请求
        json_object *json = json_tokener_parse(buffer);
        if (json) {
            bool config_changed = false;
            
            // 处理白天温度设置
            json_object *day_temp_obj;
            if (json_object_object_get_ex(json, "day_temp_target", &day_temp_obj)) {
                temp_control->day_temp_target = json_object_get_double(day_temp_obj);
                logger_log(LOG_LEVEL_INFO, "更新白天目标温度: %.1f°C", temp_control->day_temp_target);
                config_changed = true;
            }
            
            // 处理夜间温度设置
            json_object *night_temp_obj;
            if (json_object_object_get_ex(json, "night_temp_target", &night_temp_obj)) {
                temp_control->night_temp_target = json_object_get_double(night_temp_obj);
                logger_log(LOG_LEVEL_INFO, "更新夜间目标温度: %.1f°C", temp_control->night_temp_target);
                config_changed = true;
            }
            
            // 处理温度滞后设置
            json_object *hyst_obj;
            if (json_object_object_get_ex(json, "hysteresis", &hyst_obj)) {
                temp_control->temp_hysteresis = json_object_get_double(hyst_obj);
                logger_log(LOG_LEVEL_INFO, "更新温度滞后: %.1f°C", temp_control->temp_hysteresis);
                config_changed = true;
            }
            
            // 如果配置有变化，保存到文件
            if (config_changed) {
                if (save_config(temp_control) != 0) {
                    logger_log(LOG_LEVEL_ERROR, "保存配置失败");
                    // 创建错误响应
                    response_json = json_object_new_object();
                    json_object_object_add(response_json, "status", json_object_new_string("error"));
                    json_object_object_add(response_json, "message", json_object_new_string("保存配置失败"));
                } else {
                    // 创建成功响应
                    response_json = json_object_new_object();
                    json_object_object_add(response_json, "status", json_object_new_string("success"));
                    json_object_object_add(response_json, "day_temp_target", json_object_new_double(temp_control->day_temp_target));
                    json_object_object_add(response_json, "night_temp_target", json_object_new_double(temp_control->night_temp_target));
                    json_object_object_add(response_json, "hysteresis", json_object_new_double(temp_control->temp_hysteresis));
                }
            } else {
                // 没有任何设置被更新
                response_json = json_object_new_object();
                json_object_object_add(response_json, "status", json_object_new_string("error"));
                json_object_object_add(response_json, "message", json_object_new_string("没有任何设置被更新"));
            }
            
            json_object_put(json);
        } else {
            // JSON解析失败
            response_json = json_object_new_object();
            json_object_object_add(response_json, "status", json_object_new_string("error"));
            json_object_object_add(response_json, "message", json_object_new_string("无效的JSON格式"));
        }
    } else {
        // 未知的POST请求URL
        const char *error_msg = "404 Not Found";
        response = MHD_create_response_from_buffer(strlen(error_msg),
                                                 (void*)error_msg,
                                                 MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "text/plain");
        return MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    }

    // 如果没有设置响应JSON，创建一个默认的错误响应
    if (!response_json) {
        response_json = json_object_new_object();
        json_object_object_add(response_json, "status", json_object_new_string("error"));
        json_object_object_add(response_json, "message", json_object_new_string("未知错误"));
    }
    
    // 发送响应
    const char *json_str = json_object_to_json_string(response_json);
    response = MHD_create_response_from_buffer(strlen(json_str),
                                             (void*)json_str,
                                             MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    json_object_put(response_json);
    
    return ret;
}

// 请求处理入口
static enum MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                         const char *url, const char *method,
                         const char *version, const char *upload_data,
                         size_t *upload_data_size, void **con_cls) {
    if (strcmp(method, "GET") == 0) {
        return handle_get_request(cls, connection, url, method, version,
                                upload_data, upload_data_size, con_cls);
    } else if (strcmp(method, "POST") == 0) {
        return handle_post_request(cls, connection, url, method, version,
                                 upload_data, upload_data_size, con_cls);
    }
    return MHD_NO;
}

// 清理过期数据
void cleanup_old_data(void) {
    time_t now = time(NULL);
    time_t retention_time = now - (30 * 24 * 60 * 60); // 保留30天数据
    db_cleanup_old_data(retention_time);
}

// 启动Web服务器
int start_webserver(TempControl *ctrl) {
    temp_control = ctrl;
    
    // 初始化配置目录
    if (init_config_dir() != 0) {
        printf("配置目录初始化失败: %s\n", strerror(errno));
        return -1;
    }
    
    // 清理过期数据
    cleanup_old_data();
    
    // 尝试加载配置
    load_config(temp_control);  // 即使失败也继续
    
    httpd = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
                            WEB_PORT, NULL, NULL,
                            &request_handler, NULL,
                            MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)120,
                            MHD_OPTION_END);
    if (httpd == NULL) {
        printf("Web服务器启动失败: %s\n", strerror(errno));
        return -1;
    }
    printf("Web服务器已启动在端口 %d\n", WEB_PORT);
    return 0;
}

// 停止Web服务器
void stop_webserver(void) {
    if (httpd) {
        MHD_stop_daemon(httpd);
    }
}

// 更新传感器数据
void update_sensor_data(float temp, float humidity) {
    if (temp_control) {
        temp_control->current_temp = temp;
        temp_control->current_humidity = humidity;
    }
} 