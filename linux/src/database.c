#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>
#include "database.h"
#include "logger.h"
#include "webserver.h"
#include "utils.h"

static sqlite3 *db = NULL;

// 获取数据库文件的完整路径
static char* get_db_path(void) {
    char* data_path = expand_path(DATA_DIR);
    if (!data_path) {
        return NULL;
    }
    
    size_t path_len = strlen(data_path) + strlen("/temp_data.db") + 1;
    char* db_path = malloc(path_len);
    if (!db_path) {
        free(data_path);
        return NULL;
    }
    
    snprintf(db_path, path_len, "%s/temp_data.db", data_path);
    free(data_path);
    return db_path;
}

int db_init(void) {
    char* db_path = get_db_path();
    if (!db_path) {
        logger_log(LOG_LEVEL_ERROR, "无法获取数据库路径");
        return -1;
    }
    
    int rc = sqlite3_open(db_path, &db);
    free(db_path);
    
    if (rc != SQLITE_OK) {
        logger_log(LOG_LEVEL_ERROR, "无法打开数据库: %s", sqlite3_errmsg(db));
        return -1;
    }

    // 设置时区为中国时区
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "PRAGMA timezone='+08:00';", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        logger_log(LOG_LEVEL_ERROR, "设置时区失败: %s", err_msg);
        sqlite3_free(err_msg);
    }

    // 创建温度数据表
    const char *sql = "CREATE TABLE IF NOT EXISTS temp_data ("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "timestamp DATETIME DEFAULT (datetime('now', 'localtime')),"
                     "temperature REAL,"
                     "humidity REAL,"
                     "heater_state INTEGER"
                     ");";

    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        logger_log(LOG_LEVEL_ERROR, "创建表失败: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    return 0;
}

void db_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

int db_save_temp_data(float temp, float humidity, int heater_state) {
    const char *sql = "INSERT INTO temp_data (timestamp, temperature, humidity, heater_state) "
                     "VALUES (datetime('now', 'localtime'), ?, ?, ?);";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        logger_log(LOG_LEVEL_ERROR, "准备SQL语句失败: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_double(stmt, 1, temp);
    sqlite3_bind_double(stmt, 2, humidity);
    sqlite3_bind_int(stmt, 3, heater_state);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        logger_log(LOG_LEVEL_ERROR, "插入数据失败: %s", sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

char* db_get_temp_data(const char* date) {
    json_object *root = json_object_new_object();
    json_object *data_array = json_object_new_array();
    
    /* 简化的查询，使用基础的 ROW_NUMBER 和采样 */
    const char *sql = 
        "WITH samples AS ("
        "   SELECT "
        "       strftime('%Y-%m-%d %H:%M:%S', timestamp) as time,"
        "       temperature,"
        "       humidity,"
        "       heater_state,"
        "       (ROW_NUMBER() OVER (ORDER BY timestamp)) as rn,"
        "       COUNT(*) OVER () as total"
        "   FROM temp_data "
        "   WHERE timestamp >= datetime(?, '00:00:00') "
        "   AND timestamp < datetime(?, '+1 day', '00:00:00') "
        ")"
        "SELECT time, temperature, humidity, heater_state "
        "FROM samples "
        "WHERE rn = 1 OR rn = (SELECT MAX(rn) FROM samples) OR "  /* 保留首尾点 */
        "rn % CASE WHEN total > 1000 THEN (total / 300) ELSE 1 END = 0 "  /* 采样点 */
        "ORDER BY time;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        logger_log(LOG_LEVEL_ERROR, "准备SQL语句失败: %s", sqlite3_errmsg(db));
        json_object_put(root);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, date, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json_object *point = json_object_new_object();
        
        json_object_object_add(point, "time", 
            json_object_new_string((const char*)sqlite3_column_text(stmt, 0)));
        json_object_object_add(point, "temp", 
            json_object_new_double(sqlite3_column_double(stmt, 1)));
        json_object_object_add(point, "humidity", 
            json_object_new_double(sqlite3_column_double(stmt, 2)));
        json_object_object_add(point, "heater", 
            json_object_new_boolean(sqlite3_column_int(stmt, 3)));

        json_object_array_add(data_array, point);
    }

    sqlite3_finalize(stmt);
    
    json_object_object_add(root, "data", data_array);
    
    char *json_str = strdup(json_object_to_json_string(root));
    json_object_put(root);
    
    return json_str;
}

int db_cleanup_old_data(time_t before_date) {
    char date_str[20];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", localtime(&before_date));
    
    const char *sql = "DELETE FROM temp_data WHERE date(timestamp) < ?;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        logger_log(LOG_LEVEL_ERROR, "准备SQL语句失败: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, date_str, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        logger_log(LOG_LEVEL_ERROR, "清理数据失败: %s", sqlite3_errmsg(db));
        return -1;
    }

    return 0;
} 