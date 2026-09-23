# Env Vision Monitor

Qt Widgets 上位机，直接作为 WebSocket 服务端接收 `envMonitor` ESP32 固件上传的实时数据。

## 构建与运行

需要 Qt 6 的 Widgets、Network、WebSockets 模块：

```bash
qmake6 Monitor.pro
make -j4
./Monitor.app/Contents/MacOS/Monitor
```

程序默认监听 `0.0.0.0:8080`，监听端口可在“连接设置”中修改。ESP32 固件中的地址应填写为运行本程序的电脑局域网地址，例如：

```cpp
#define SERVER_URL "ws://192.168.1.100:8080/"
```

## 数据协议

程序原生支持下位机当前使用的紧凑批量 JSON。顶层包含 `frame_seq`、
`sent_at_ms` 和 `sensors`；为减少传输量，二维样本不再携带 `fields`：

- `dht`: `temperature`、`humidity`
- `light`: `light1`、`light2`
- `pir`: 人体活动状态
- `ags02`: TVOC
- `mpu6050`: `ax/ay/az`、`gx/gy/gz`
- `sound`: 声压数据

二维样本的列顺序固定为：

- `dht`: `[temperature, humidity]`
- `light`: `[light1, light2]`
- `mpu6050`: `[ax, ay, az, gx, gy, gz]`

每个样本的时间按 `first_sample_time_ms + index * sample_period_ms` 还原。界面数据仅保存在内存中，尚未引入数据库或中间服务器。

解析器仍兼容旧版包含 `schema_version` 和 `fields` 的数据帧；如果数据块显式提供 `fields`，将优先使用数据帧中的定义。
