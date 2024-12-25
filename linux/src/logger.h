#ifndef LOGGER_H
#define LOGGER_H

// 日志级别定义
typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_ERROR
} LogLevel;

// 初始化日志系统
void logger_init(const char* ident);

// 关闭日志系统
void logger_cleanup(void);

// 写入日志
void logger_log(LogLevel level, const char* format, ...);

#endif 