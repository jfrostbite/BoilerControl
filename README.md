# 壁挂炉温控系统

这是一个基于 Linux 的壁挂炉温控系统，使用 AHT10 传感器监测温度和湿度，通过 MQTT 控制 ESP8266 来操作壁挂炉。系统提供 Web 界面进行监控和控制。

## 功能特点

- 实时温度和湿度监测
- 白天/夜间温度自动调节
- Web 界面实时监控
- 温度曲线图表显示
- 历史数据查看
- 系统日志记录
- LED 状态指示

## 系统要求

### 硬件要求
- Linux 开发板（已在 Orange Pi Zero2 上测试）
- AHT10 温湿度传感器
- ESP8266 模块（用于控制壁挂炉）
- I2C 接口支持

### 软件依赖
```bash
# Debian/Ubuntu 系统依赖安装
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    gcc-aarch64-linux-gnu \
    libmosquitto-dev \
    libmicrohttpd-dev \
    libjson-c-dev \
    libsqlite3-dev \
    mosquitto \
    mosquitto-clients
```

## 编译安装

1. 克隆代码：
```bash
git clone [repository_url]
cd BoilerController
```

2. 编译：
```bash
cd linux
make
```

3. 安装：
```bash
# 创建配置目录
sudo mkdir -p /etc/boiler_control
sudo mkdir -p /var/lib/boiler_control

# 复制可执行文件
sudo cp temp_control /usr/local/bin/

# 设置权限
sudo chown -R $USER:$USER /etc/boiler_control
sudo chown -R $USER:$USER /var/lib/boiler_control
```

4. 配置 MQTT：
```bash
# 编辑 MQTT 配置文件
sudo nano /etc/mosquitto/mosquitto.conf

# 添加以下内容
listener 1883
allow_anonymous true

# 重启 MQTT 服务
sudo systemctl restart mosquitto
```

5. 创建系统服务：
```bash
sudo nano /etc/systemd/system/boiler-control.service
```

添加以下内容：
```ini
[Unit]
Description=Boiler Control System
After=network.target mosquitto.service

[Service]
ExecStart=/usr/local/bin/temp_control
Restart=always
User=root
Group=root
Environment=DATA_DIR=/var/lib/boiler_control
Environment=CONFIG_DIR=/etc/boiler_control

[Install]
WantedBy=multi-user.target
```

6. 启动服务：
```bash
sudo systemctl daemon-reload
sudo systemctl enable boiler-control
sudo systemctl start boiler-control
```

## 使用说明

1. Web 界面访问：
   - 打开浏览器访问 `http://[设备IP]:8080`

2. 温度设置：
   - 白天温度（6:00-22:00）
   - 夜间温度（22:00-6:00）
   - 温度滞后值

3. 数据存储：
   - 温度数据保存在 `/var/lib/boiler_control/temp_data.db`
   - 配置文件保存在 `/etc/boiler_control/config.json`
   - 系统日志保存在 `/var/log/syslog`

4. LED 指示：
   - 白天：加热时 LED 闪烁
   - 夜间：LED 不亮
   - 关闭状态：LED 不亮

## 故障排除

1. MQTT 连接问题：
```bash
# 检查 MQTT 服务状态
sudo systemctl status mosquitto

# 检查 MQTT 日志
sudo tail -f /var/log/mosquitto/mosquitto.log
```

2. 传感器问题：
```bash
# 检查 I2C 设备
i2cdetect -y 0
```

3. 系统日志查看：
```bash
# 查看服务日志
sudo journalctl -u boiler-control -f
```

## 开发说明

- 主程序源码在 `linux/src` 目录
- Web 界面代码在 `linux/src/index_html.h`
- 使用 SQLite 数据库存储温度数据
- 使用 libmicrohttpd 提供 Web 服务
- 使用 Mosquitto 进行 MQTT 通信

## 注意事项

1. 安全考虑：
   - Web 服务没有身份验证，建议只在内网使用
   - MQTT 服务建议设置用户名和密码

2. 数据备份：
   - 建议定期备份 `/var/lib/boiler_control` 目录
   - 可以使用 cron 任务自动备份

3. 性能优化：
   - 数据库会自动清理30天前的数据
   - Web 界面使用数据采样优化显示效果

## 许可证

本项目采用 MIT 许可证。详见 [LICENSE](LICENSE) 文件。

## 版权

Copyright (c) 2023 Kevin. All rights reserved.
