#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include "logger.h"
#include "webserver.h"

void logger_init(const char* ident) {
    openlog(ident, LOG_PID | LOG_CONS, LOG_USER);
}

void logger_cleanup(void) {
    closelog();
}

void logger_log(LogLevel level, const char* format, ...) {
    va_list args;
    char message[512];
    int syslog_priority;
    
    // 格式化消息
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // 根据日志级别设置 syslog 优先级
    switch (level) {
        case LOG_LEVEL_ERROR:
            syslog_priority = LOG_ERR;
            break;
        case LOG_LEVEL_INFO:
        default:
            syslog_priority = LOG_INFO;
            break;
    }
    
    // 写入 syslog
    syslog(syslog_priority, "%s", message);
    
    // 写入 web 日志
    add_log("%s", message);
} 