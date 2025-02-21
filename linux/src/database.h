#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <time.h>

// 初始化数据库
int db_init(void);

// 关闭数据库
void db_close(void);

// 保存温度数据
int db_save_temp_data(float temp, float humidity, int heater_state);

// 获取指定日期的温度数据
char* db_get_temp_data(const char* date);

// 清理指定日期之前的数据
int db_cleanup_old_data(time_t before_date);

#endif 