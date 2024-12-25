#ifndef AHT10_H
#define AHT10_H

#include <stdint.h>

// AHT10 I2C 地址
#define AHT10_ADDRESS      0x38

// AHT10 命令
#define AHT10_INIT        0xE1
#define AHT10_MEASURE     0xAC
#define AHT10_RESET       0xBA

// 函数声明
int aht10_init(int i2c_bus);
int aht10_read_sensor(int fd, float *temperature, float *humidity);
void aht10_close(int fd);

#endif 