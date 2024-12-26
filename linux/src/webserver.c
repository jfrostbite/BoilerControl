#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include "webserver.h"

static struct MHD_Daemon *httpd;
static TempControl *temp_control;
static LogEntry logs[MAX_LOGS];  // 日志数组
static int log_count = 0;        // 当前日志数量

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
    json_object *json = json_object_new_object();
    json_object_object_add(json, "day_temp_target", json_object_new_double(ctrl->day_temp_target));
    json_object_object_add(json, "night_temp_target", json_object_new_double(ctrl->night_temp_target));
    json_object_object_add(json, "hysteresis", json_object_new_double(ctrl->temp_hysteresis));
    json_object_object_add(json, "day_start_hour", json_object_new_int(ctrl->day_start_hour));
    json_object_object_add(json, "night_start_hour", json_object_new_int(ctrl->night_start_hour));
    
    const char *json_str = json_object_to_json_string(json);
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        printf("保存配置失败: %s\n", strerror(errno));
        json_object_put(json);
        return -1;
    }
    
    fprintf(fp, "%s\n", json_str);
    fclose(fp);
    json_object_put(json);
    printf("配置已保存到 %s\n", CONFIG_FILE);
    return 0;
}

// 从文件加载配置
int load_config(TempControl *ctrl) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("加载配置失败: %s\n", strerror(errno));
        printf("使用默认配置\n");
        // 使用默认值
        ctrl->day_temp_target = 21.0;
        ctrl->night_temp_target = 20.0;
        ctrl->temp_hysteresis = 0.5;
        ctrl->day_start_hour = 6;    // 早上6点
        ctrl->night_start_hour = 22; // 晚上10点
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
    return 0;
}

// HTML页面内容
static const char* HTML_PAGE = 
"<!DOCTYPE html>"
"<html><head><meta charset='utf-8'>"
"<title>壁挂炉控制</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
"<script src='https://cdn.jsdelivr.net/npm/moment@2.29.4/moment.min.js'></script>"
"<script src='https://cdn.jsdelivr.net/npm/chartjs-adapter-moment@1.0.1/dist/chartjs-adapter-moment.min.js'></script>"
"<style>"
"body {"
"    margin: 0;"
"    padding: 20px;"
"    font-family: Arial, sans-serif;"
"    background: #f0f2f5;"
"    min-height: 100vh;"
"}"
".container {"
"    max-width: 800px;"
"    margin: 0 auto;"
"}"
".card {"
"    background: #fff;"
"    padding: 24px;"
"    margin: 16px 0;"
"    border-radius: 12px;"
"    box-shadow: 0 2px 8px rgba(0,0,0,0.1);"
"}"
".chart-container {"
"    position: relative;"
"    height: 300px;"  // 固定高度
"    margin: 20px 0;"
"}"
".card h2 {"
"    margin: 0 0 20px 0;"
"    color: #1a1a1a;"
"    font-size: 1.5em;"
"}"
".status {"
"    display: grid;"
"    grid-template-columns: repeat(3, 1fr);"
"    gap: 16px;"
"    text-align: center;"
"}"
".status-item {"
"    padding: 16px;"
"    background: #f8f9fa;"
"    border-radius: 8px;"
"    font-size: 1.2em;"
"}"
".status-label {"
"    color: #666;"
"    font-size: 0.9em;"
"    margin-bottom: 8px;"
"}"
".status-value {"
"    color: #1a1a1a;"
"    font-weight: bold;"
"}"
".control {"
"    display: flex;"
"    align-items: center;"
"    justify-content: space-between;"
"    margin: 16px 0;"
"    padding: 16px;"
"    background: #f8f9fa;"
"    border-radius: 8px;"
"}"
".control-label {"
"    font-size: 1.1em;"
"    color: #1a1a1a;"
"}"
".control-input {"
"    display: flex;"
"    align-items: center;"
"    gap: 12px;"
"}"
"input {"
"    width: 100px;"
"    padding: 8px 12px;"
"    border: 1px solid #ddd;"
"    border-radius: 6px;"
"    font-size: 1em;"
"    text-align: center;"
"}"
"button {"
"    padding: 8px 20px;"
"    border: none;"
"    border-radius: 6px;"
"    background: #0066ff;"
"    color: white;"
"    font-size: 1em;"
"    cursor: pointer;"
"    transition: all 0.2s;"
"    position: relative;"
"}"
"button:hover {"
"    background: #0052cc;"
"}"
"button:disabled {"
"    background: #ccc;"
"    cursor: not-allowed;"
"}"
"button.loading {"
"    padding-right: 40px;"
"}"
"button.loading:after {"
"    content: '';"
"    position: absolute;"
"    right: 10px;"
"    top: 50%;"
"    width: 20px;"
"    height: 20px;"
"    margin-top: -10px;"
"    border: 2px solid #fff;"
"    border-top-color: transparent;"
"    border-radius: 50%;"
"    animation: spin 1s linear infinite;"
"}"
"@keyframes spin {"
"    to { transform: rotate(360deg); }"
"}"
".toast {"
"    position: fixed;"
"    bottom: 20px;"
"    left: 50%;"
"    transform: translateX(-50%);"
"    background: rgba(0,0,0,0.8);"
"    color: white;"
"    padding: 12px 24px;"
"    border-radius: 6px;"
"    font-size: 1em;"
"    opacity: 0;"
"    transition: opacity 0.3s;"
"    pointer-events: none;"
"}"
".toast.show {"
"    opacity: 1;"
"}"
".heater-on {"
"    color: #ff4d4f;"
"}"
".heater-off {"
"    color: #52c41a;"
"}"
".logs {"
"    background: #f8f9fa;"
"    border-radius: 8px;"
"    padding: 16px;"
"    max-height: 400px;"
"    overflow-y: auto;"
"    font-family: monospace;"
"    font-size: 0.9em;"
"    line-height: 1.5;"
"}"
".log-entry {"
"    padding: 4px 0;"
"    border-bottom: 1px solid #eee;"
"}"
".log-time {"
"    color: #666;"
"    margin-right: 8px;"
"}"
"</style></head>"
"<body>"
"<div class='container'>"
"    <div class='card'>"
"        <h2>系统状态</h2>"
"        <div class='status'>"
"            <div class='status-item'>"
"                <div class='status-label'>当前温度</div>"
"                <div class='status-value' id='temp'>--°C</div>"
"            </div>"
"            <div class='status-item'>"
"                <div class='status-label'>当前湿度</div>"
"                <div class='status-value' id='humidity'>--%</div>"
"            </div>"
"            <div class='status-item'>"
"                <div class='status-label'>加热器状态</div>"
"                <div class='status-value' id='heater'>--</div>"
"            </div>"
"        </div>"
"    </div>"
"    <div class='card'>"
"        <h2>温度控制</h2>"
"        <div class='control'>"
"            <div class='control-label'>白天温度 (6:00-22:00)</div>"
"            <div class='control-input'>"
"                <input type='number' id='day_target' step='0.1' min='5' max='30'>"
"                <span>°C</span>"
"                <button onclick='setDayTarget()'>设置</button>"
"            </div>"
"        </div>"
"        <div class='control'>"
"            <div class='control-label'>夜间温度 (22:00-6:00)</div>"
"            <div class='control-input'>"
"                <input type='number' id='night_target' step='0.1' min='5' max='30'>"
"                <span>°C</span>"
"                <button onclick='setNightTarget()'>设置</button>"
"            </div>"
"        </div>"
"        <div class='control'>"
"            <div class='control-label'>温度滞后</div>"
"            <div class='control-input'>"
"                <input type='number' id='hysteresis' step='0.1' min='0.1' max='2.0'>"
"                <span>°C</span>"
"                <button onclick='setHysteresis()'>设置</button>"
"            </div>"
"        </div>"
"    </div>"
"    <div class='card'>"
"        <h2>温度曲线</h2>"
"        <div class='chart-container'>"
"            <canvas id='tempChart'></canvas>"
"        </div>"
"    </div>"
"    <div class='card'>"
"        <h2>系统日志</h2>"
"        <div class='logs' id='logs'></div>"
"    </div>"
"</div>"
"<div id='toast' class='toast'></div>"
"<script>"
"var tempData = {"
"    labels: [],"
"    datasets: [{"
"        label: '温度 (°C)',"
"        data: [],"
"        borderColor: 'rgb(75, 192, 192)',"
"        backgroundColor: 'rgba(75, 192, 192, 0.1)',"
"        fill: true,"
"        tension: 0.3,"
"        yAxisID: 'y',"
"        pointRadius: 0,"
"        borderWidth: 2"
"    }, {"
"        label: '加热状态',"
"        data: [],"
"        borderColor: 'rgb(255, 99, 132)',"
"        backgroundColor: 'rgba(255, 99, 132, 0.1)',"
"        fill: true,"
"        stepped: true,"
"        yAxisID: 'y1',"
"        pointRadius: 0,"
"        borderWidth: 2"
"    }]"
"};"
""
"var tempConfig = {"
"    type: 'line',"
"    data: tempData,"
"    options: {"
"        responsive: true,"
"        maintainAspectRatio: false,"
"        interaction: {"
"            mode: 'index',"
"            intersect: false"
"        },"
"        plugins: {"
"            legend: {"
"                position: 'top',"
"                labels: {"
"                    usePointStyle: true,"
"                    padding: 20"
"                }"
"            }"
"        },"
"        scales: {"
"            x: {"
"                type: 'time',"
"                time: {"
"                    unit: 'minute',"
"                    displayFormats: {"
"                        minute: 'HH:mm'"
"                    }"
"                },"
"                grid: {"
"                    display: false"
"                },"
"                title: {"
"                    display: true,"
"                    text: '时间'"
"                }"
"            },"
"            y: {"
"                type: 'linear',"
"                display: true,"
"                position: 'left',"
"                title: {"
"                    display: true,"
"                    text: '温度 (°C)'"
"                }"
"            },"
"            y1: {"
"                type: 'linear',"
"                display: true,"
"                position: 'right',"
"                min: -0.1,"
"                max: 1.1,"
"                grid: {"
"                    drawOnChartArea: false"
"                },"
"                title: {"
"                    display: true,"
"                    text: '加热状态'"
"                },"
"                ticks: {"
"                    callback: function(value) {"
"                        return value > 0.5 ? '开启' : '关闭';"
"                    }"
"                }"
"            }"
"        }"
"    }"
"};"
""
"var ctx = document.getElementById('tempChart').getContext('2d');"
"var tempChart = new Chart(ctx, tempConfig);"
""
"function updateChart(temp, heaterState) {"
"    var now = new Date();"
"    tempData.labels.push(now);"
"    tempData.datasets[0].data.push(temp);"
"    tempData.datasets[1].data.push(heaterState ? 1 : 0);"
"    "
"    var twoHoursAgo = now - 2 * 60 * 60 * 1000;"
"    var cutoffIndex = tempData.labels.findIndex(function(time) {"
"        return time > twoHoursAgo;"
"    });"
"    "
"    if (cutoffIndex > 0) {"
"        tempData.labels.splice(0, cutoffIndex);"
"        tempData.datasets[0].data.splice(0, cutoffIndex);"
"        tempData.datasets[1].data.splice(0, cutoffIndex);"
"    }"
"    "
"    tempChart.update();"
"}"
""
"function updateStatus() {"
"    fetch('/api/status').then(r=>r.json()).then(data=>{"
"        document.getElementById('temp').textContent = `${data.current_temp.toFixed(1)}°C`;"
"        document.getElementById('humidity').textContent = `${data.current_humidity.toFixed(1)}%`;"
"        document.getElementById('heater').textContent = data.heater_state ? '开启' : '关闭';"
"        document.getElementById('heater').className = "
"            'status-value ' + (data.heater_state ? 'heater-on' : 'heater-off');"
"        updateChart(data.current_temp, data.heater_state);"
"    }).catch(err => console.error('更新状态失败:', err));"
"}"
"function loadSettings() {"
"    fetch('/api/status').then(r=>r.json()).then(data=>{"
"        document.getElementById('day_target').value = data.day_temp_target.toFixed(1);"
"        document.getElementById('night_target').value = data.night_temp_target.toFixed(1);"
"        document.getElementById('hysteresis').value = data.hysteresis.toFixed(1);"
"    }).catch(err => console.error('加载设置失败:', err));"
"}"
"function showToast(message, duration = 2000) {"
"    const toast = document.getElementById('toast');"
"    toast.textContent = message;"
"    toast.classList.add('show');"
"    setTimeout(() => toast.classList.remove('show'), duration);"
"}"
"function setButtonLoading(btn, loading) {"
"    btn.disabled = loading;"
"    if (loading) {"
"        btn.classList.add('loading');"
"    } else {"
"        btn.classList.remove('loading');"
"    }"
"}"
"function setDayTarget() {"
"    const btn = event.target;"
"    const temp = document.getElementById('day_target').value;"
"    setButtonLoading(btn, true);"
"    fetch('/api/settings', {"
"        method: 'POST',"
"        headers: {'Content-Type': 'application/json'},"
"        body: JSON.stringify({day_temp_target: parseFloat(temp)})"
"    }).then(r => r.json())"
"      .then(data => {"
"        if(data.status === 'success') {"
"            document.getElementById('day_target').value = data.day_temp_target.toFixed(1);"
"            showToast('白天温度已更新');"
"        }"
"    }).catch(err => {"
"        console.error('设置白天温度失败:', err);"
"        showToast('设置失败，请重试');"
"    }).finally(() => {"
"        setButtonLoading(btn, false);"
"    });"
"}"
"function setNightTarget() {"
"    const btn = event.target;"
"    const temp = document.getElementById('night_target').value;"
"    setButtonLoading(btn, true);"
"    fetch('/api/settings', {"
"        method: 'POST',"
"        headers: {'Content-Type': 'application/json'},"
"        body: JSON.stringify({night_temp_target: parseFloat(temp)})"
"    }).then(r => r.json())"
"      .then(data => {"
"        if(data.status === 'success') {"
"            document.getElementById('night_target').value = data.night_temp_target.toFixed(1);"
"            showToast('夜间温度已更新');"
"        }"
"    }).catch(err => {"
"        console.error('设置夜间温度失败:', err);"
"        showToast('设置失败，请重试');"
"    }).finally(() => {"
"        setButtonLoading(btn, false);"
"    });"
"}"
"function setHysteresis() {"
"    const btn = event.target;"
"    const hyst = document.getElementById('hysteresis').value;"
"    setButtonLoading(btn, true);"
"    fetch('/api/settings', {"
"        method: 'POST',"
"        headers: {'Content-Type': 'application/json'},"
"        body: JSON.stringify({hysteresis: parseFloat(hyst)})"
"    }).then(r => r.json())"
"      .then(data => {"
"        if(data.status === 'success') {"
"            document.getElementById('hysteresis').value = data.hysteresis.toFixed(1);"
"            showToast('温度滞后已更新');"
"        }"
"    }).catch(err => {"
"        console.error('设置温度滞后失败:', err);"
"        showToast('设置失败，请重试');"
"    }).finally(() => {"
"        setButtonLoading(btn, false);"
"    });"
"}"
"function updateLogs() {"
"    fetch('/api/logs').then(r=>r.json()).then(logs=>{"
"        const logsHtml = logs.map(log => `"
"            <div class='log-entry'>"
"                <span class='log-time'>${log.time}</span>"
"                <span class='log-msg'>${log.msg}</span>"
"            </div>"
"        `).join('');"
"        document.getElementById('logs').innerHTML = logsHtml;"
"    }).catch(err => console.error('更新日志失败:', err));"
"}"
"loadSettings();"
"setInterval(updateStatus, 5000);"
"updateStatus();"
"setInterval(updateLogs, 5000);"  // 每5秒更新一次日志
"updateLogs();"  // 立即更新一次
"</script>"
"</body></html>";

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

    // 处理 OPTIONS 请求
    if (strcmp(method, "OPTIONS") == 0) {
        response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    if (*con_cls == NULL) {
        *con_cls = &dummy;
        return MHD_YES;
    }

    if (*upload_data_size != 0) {
        // 处理上传的数据
        static char buffer[1024];
        if (*upload_data_size > sizeof(buffer) - 1)
            return MHD_NO;
        
        memcpy(buffer, upload_data, *upload_data_size);
        buffer[*upload_data_size] = '\0';
        *upload_data_size = 0;
        
        // 解析JSON请求
        json_object *json = json_tokener_parse(buffer);
        if (json) {
            json_object *day_temp_obj;
            if (json_object_object_get_ex(json, "day_temp_target", &day_temp_obj)) {
                temp_control->day_temp_target = json_object_get_double(day_temp_obj);
                printf("更新白天目标温度: %.1f°C\n", temp_control->day_temp_target);
                save_config(temp_control);
            }
            json_object *night_temp_obj;
            if (json_object_object_get_ex(json, "night_temp_target", &night_temp_obj)) {
                temp_control->night_temp_target = json_object_get_double(night_temp_obj);
                printf("更新夜间目标温度: %.1f°C\n", temp_control->night_temp_target);
                save_config(temp_control);
            }
            json_object *hyst_obj;
            if (json_object_object_get_ex(json, "hysteresis", &hyst_obj)) {
                temp_control->temp_hysteresis = json_object_get_double(hyst_obj);
                printf("更新温度滞后: %.1f°C\n", temp_control->temp_hysteresis);
            }
            json_object_put(json);
            
            // 保存配置到文件
            if (save_config(temp_control) != 0) {
                printf("保存配置失败\n");
            }
        }
        
        return MHD_YES;
    }

    // 创建响应
    json_object *response_json = json_object_new_object();
    json_object_object_add(response_json, "status", json_object_new_string("success"));
    json_object_object_add(response_json, "day_temp_target", json_object_new_double(temp_control->day_temp_target));
    json_object_object_add(response_json, "night_temp_target", json_object_new_double(temp_control->night_temp_target));
    json_object_object_add(response_json, "hysteresis", json_object_new_double(temp_control->temp_hysteresis));
    
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

// 启动Web服务器
int start_webserver(TempControl *ctrl) {
    temp_control = ctrl;
    
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