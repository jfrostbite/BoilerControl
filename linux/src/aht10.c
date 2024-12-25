#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include "aht10.h"

int aht10_init(int i2c_bus) {
    char filename[20];
    snprintf(filename, 19, "/dev/i2c-%d", i2c_bus);
    
    printf("Opening I2C device: %s\n", filename);
    int fd = open(filename, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open i2c bus: %s\n", strerror(errno));
        return -1;
    }
    printf("I2C device opened successfully, fd=%d\n", fd);

    printf("Setting I2C slave address to 0x%02X\n", AHT10_ADDRESS);
    if (ioctl(fd, I2C_SLAVE, AHT10_ADDRESS) < 0) {
        fprintf(stderr, "Failed to acquire bus access: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("I2C slave address set successfully\n");

    // 先发送软复位命令
    uint8_t reset_cmd = AHT10_RESET;
    printf("Sending reset command: 0x%02X\n", reset_cmd);
    if (write(fd, &reset_cmd, 1) != 1) {
        fprintf(stderr, "Failed to reset AHT10: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    usleep(20000); // 等待20ms

    // 发送初始化命令
    uint8_t cmd[] = {AHT10_INIT, 0x08, 0x00};
    printf("Sending init command: 0x%02X 0x%02X 0x%02X\n", cmd[0], cmd[1], cmd[2]);
    write(fd, cmd, 3);  // 忽略返回值
    
    printf("Init sequence completed\n");
    usleep(100000); // 等待100ms
    return fd;
}

int aht10_read_sensor(int fd, float *temperature, float *humidity) {
    uint8_t cmd[] = {AHT10_MEASURE, 0x33, 0x00};
    uint8_t data[6];

    // 发送测量命令
    if (write(fd, cmd, 3) != 3) {
        return -1;
    }

    usleep(80000); // 等待80ms完成测量

    // 读取数据
    if (read(fd, data, 6) != 6) {
        return -1;
    }

    // 检查状态位
    if (data[0] & 0x80) {
        return -2; // 设备忙
    }

    // 计算湿度和温度
    uint32_t humidity_raw = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t temp_raw = ((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    *humidity = (float)humidity_raw * 100 / 0x100000;
    *temperature = (float)temp_raw * 200 / 0x100000 - 50;

    return 0;
}

void aht10_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
} 