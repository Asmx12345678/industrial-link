# 无真实硬件验证方案

没有 PLC、传感器或 AGV 时，项目采用协议级数字孪生验证。它不模拟设备外观，而是按照
TCP/UDP、Modbus/TCP 和 MQTT 的线上字节与状态机行为生成数据，因此适合验证网关软件；
简历与答辩中应称为“模拟设备/模拟 PLC 联调”，不能写成“真实产线部署”。

## 可在纯软件环境完成的验证

| 层级 | 工具 | 可证明内容 |
|---|---|---|
| 单元/协议 | `make test` | MQTT 编解码、Modbus 报文、去重、告警、存储 |
| TCP/UDP 设备 | `simulator/simulator.py` | 握手、遥测、ACK、心跳、超时、并发连接 |
| PLC | `simulator/modbus_server.py` | Holding Register 读取、轮询、断线退避 |
| MQTT | Mosquitto + `mqtt_publisher.py` | Broker 订阅、QoS 0/1、协议桥转发 |
| 故障 | `--disconnect-ratio`、`--malformed-ratio` | 主动断线、自动重连、错误帧识别 |
| 性能 | `scripts/benchmark.py` | 并发数、吞吐、ACK 比例、P50/P95/P99 RTT |
| 运维 | Docker Compose、REST 健康检查 | 一键部署、依赖编排、状态可观测性 |
| 人机界面 | Qt6 Monitor | 设备列表、指标、事件、命令下发 |

## 推荐验收顺序

```bash
# 1. 构建与回归
make -j2
make test

# 2. 500 设备并发
python3 scripts/benchmark.py --devices 500 --rate 10 --duration 10 \
  --output benchmark/hardwareless-load.json

# 3. 断线与错误报文
python3 scripts/benchmark.py --devices 100 --rate 5 --duration 60 \
  --disconnect-ratio 0.02 --malformed-ratio 0.01 \
  --output benchmark/hardwareless-fault.json

# 4. MQTT + Modbus 全协议链路
docker compose up --build -d
python3 simulator/mqtt_publisher.py --devices 5 --duration 30
docker compose ps
docker compose logs --tail 100 gateway mqtt-bridge modbus-bridge
docker compose down
```

长稳测试把第 2 步的 `--duration` 改为 `28800`（8 小时）或 `86400`（24 小时），并记录
机器、参数、开始/结束时间、网关指标和退出码。不要让睡眠或合盖中断测试。

## 软件模拟不能替代的内容

- RS-485/CAN 等物理层接线、终端电阻、电平和串口驱动。
- 具体厂商寄存器表、私有协议字段及固件缺陷。
- 现场网络拥塞、交换机策略、无线衰落、电磁干扰与时钟漂移。
- 真实控制动作的安全联锁和失效保护。

这些内容应在后续拿到设备后做一次 Hardware-in-the-Loop 补充验证。作品集阶段可选购一块
ESP32 和 USB-RS485 转换器作为加分项，但不是完成本项目的前置条件。

## 简历与面试表述

推荐表述：

> 基于虚拟设备、模拟 Modbus/TCP PLC 与 Mosquitto 构建协议级数字孪生测试环境，完成
> TCP/UDP/MQTT/Modbus 多协议联调、500 连接压力测试及断线/错误报文故障注入。

若尚未接入真实设备，不使用“部署于真实产线”“适配某厂商 PLC”或“完成硬件在环验证”等表述。
