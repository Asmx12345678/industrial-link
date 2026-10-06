# IndustrialLink

[![CI](https://github.com/Asmx12345678/industrial-link/actions/workflows/ci.yml/badge.svg)](https://github.com/Asmx12345678/industrial-link/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](CMakeLists.txt)

面向工业设备的跨平台通信网关与监控客户端。项目以 C++17 网关为核心，统一接入 TCP、UDP、MQTT 和 Modbus/TCP 设备，提供连接生命周期管理、协议转换、遥测、告警、指令下发、REST API、Qt6 桌面监控和并发设备模拟。

> 项目定位：作品集与内网工程原型。当前结果来自虚拟设备、模拟 PLC 与 MQTT Broker，
> 不代表真实产线部署；未经认证、TLS 和权限控制加固，请勿将管理接口直接暴露到公网。

## 已实现能力

- C++17 单线程 Reactor 网关，基于非阻塞 Socket 与 `select` 管理设备连接。
- TCP 长连接、UDP 遥测、应用层握手与统一行协议。
- 心跳、超时剔除、设备重连、重复序号过滤和接收缓冲区保护。
- 动态协议插件：通过稳定 C ABI 加载 `.so`、`.dylib` 或 `.dll`。
- MQTT 3.1.1 协议桥：原生实现 CONNECT、SUBSCRIBE、QoS 0/1 PUBLISH 和 PUBACK。
- Modbus/TCP 协议桥：轮询 Holding Registers，支持多个 PLC 配置与断线退避。
- REST API、内置 Web 状态页和 Qt6 桌面监控客户端。
- 设备指令下发与 ACK 事件。
- 温度、电压规则告警。
- JSONL 审计事件日志。
- SQLite/WAL 持久化事件历史。
- TCP/UDP、MQTT、Modbus/TCP 设备模拟与故障注入。
- 自动化端到端测试、基准测试脚本、Docker Compose 与 GitHub Actions。

## 架构

```text
TCP/UDP 设备 ───────────────┐
MQTT Broker → MQTT Bridge ─┤
PLC → Modbus Bridge ───────┤
                           ▼
                  IndustrialLink Gateway
          ┌────────────────────────────────┐
          │ Session / Heartbeat / Dedupe   │
          │ Protocol Plugin / Alarm Rules  │
          │ Event Log / REST API / Command │
          └───────────────┬────────────────┘
                          │ HTTP/JSON
                   ┌──────┴──────┐
                   │ Qt6 Monitor │
                   │ Web Status  │
                   └─────────────┘
```

详见 [架构文档](docs/ARCHITECTURE.md)、[通信协议](docs/PROTOCOL.md)、
[无硬件验证方案](docs/HARDWARELESS_VALIDATION.md) 和 [验证记录](docs/VALIDATION.md)。

## 快速开始

### 1. 本机编译

依赖：支持 C++17 的编译器、GNU Make、Python 3.9+。

```bash
make -j2
make test
```

Linux 会生成：

```text
build/industrial-gateway
build/libvendor_pipe.so
```

macOS 会生成 `.dylib` 插件。若 Command Line Tools 的默认 SDK 与链接器版本不匹配，Makefile 会优先使用已安装的稳定版 macOS 15.4 SDK，也可以通过 `MACOS_SDK=/path/to/sdk make` 覆盖。

### 2. 启动演示

```bash
make demo
```

或者分别启动：

```bash
./build/industrial-gateway --plugin ./build/libvendor_pipe.dylib
python3 simulator/simulator.py --devices 10 --rate 2 --duration 30
```

Linux 将插件后缀换成 `.so`。浏览器打开：

```text
http://127.0.0.1:8080
```

### 3. Docker 全协议环境

```bash
docker compose up --build
```

如果所在网络无法直连 Docker Hub，可通过兼容镜像代理启动，而不必修改镜像定义：

```bash
DOCKER_IMAGE_PREFIX=docker.m.daocloud.io/library/ docker compose up --build
```

该环境包含网关、Mosquitto、MQTT 桥、Modbus/TCP PLC 模拟器和 Modbus 桥。启动后可以运行 MQTT 发布器：

```bash
python3 simulator/mqtt_publisher.py --devices 5 --duration 30
```

### 4. Qt6 客户端

需要 Qt 6.4+ 的 Widgets 与 Network 模块：

```bash
cmake -S . -B build-qt -DINDUSTRIAL_LINK_BUILD_QT_CLIENT=ON
cmake --build build-qt --parallel
./build-qt/industrial-monitor --url http://127.0.0.1:8080
```

Qt 客户端提供设备表、网关指标、实时事件和远程指令下发。

macOS 没有系统包管理器时，也可以把 CMake 与 Qt 安装到项目私有目录：

```bash
./scripts/bootstrap_macos_qt.sh
./scripts/build_qt_local.sh
./build/cmake/industrial-monitor --url http://127.0.0.1:8080
```

这两个脚本不会写入系统目录；默认安装 CMake 4.x 与 Qt 6.8.3 到 `.tools/`。

## 网关参数

```text
--bind ADDRESS          监听地址，默认 127.0.0.1
--tcp-port PORT         设备 TCP 端口，默认 9000
--udp-port PORT         设备 UDP 端口，默认 9001
--http-port PORT        状态页/API 端口，默认 8080
--heartbeat SEC         心跳间隔，默认 5 秒
--timeout SEC           设备超时，默认 15 秒
--max-clients COUNT     TCP 客户端上限，默认 2048
--event-log PATH        JSONL 事件日志
--database PATH         SQLite 事件数据库
--plugin PATH           加载动态协议插件，可重复指定
```

生产部署时应使用 `--bind 0.0.0.0`，并在反向代理或防火墙层保护管理 API。

## REST API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/health` | 存活探针 |
| GET | `/api/metrics` | 网关指标 |
| GET | `/api/devices` | 在线设备 |
| GET | `/api/events` | 最近事件与告警 |
| GET | `/api/history?device_id=...&limit=100` | SQLite 历史事件 |
| GET | `/api/plugins` | 已加载插件 |
| POST | `/api/command` | 向在线 TCP 设备下发指令 |

命令示例：

```bash
curl -X POST http://127.0.0.1:8080/api/command \
  -H 'Content-Type: application/json' \
  -d '{"device_id":"sensor-0001","command":"set_rate=5"}'
```

## 设备模拟与故障注入

```bash
# 500 个连接，每台每秒 10 条数据
python3 simulator/simulator.py --devices 500 --rate 10 --duration 10

# 模拟 2% 主动断线和 1% 错误报文
python3 simulator/simulator.py --devices 100 --rate 5 --duration 30 \
  --disconnect-ratio 0.02 --malformed-ratio 0.01

# UDP 设备
python3 simulator/simulator.py --protocol udp --devices 100 --rate 10 --duration 10

# 使用动态协议插件
python3 simulator/simulator.py --device-protocol plugin:vendor_pipe --devices 20
```

没有真实 PLC、传感器或 AGV 并不妨碍完成协议层和软件工程层验证。本项目把 TCP/UDP
虚拟设备、Modbus/TCP PLC、MQTT 发布器视为协议级数字孪生；功能、并发、断线重连、
错误报文和长稳测试均可复现。真实硬件仍用于最后确认电气接口、厂商私有字段、时序抖动
和现场电磁/网络干扰，二者边界见 [无硬件验证方案](docs/HARDWARELESS_VALIDATION.md)。

## 实测结果

本仓库的 [最新基准结果](benchmark/latest.json) 来自 10 核 Apple Silicon 本机：

- 500 个 TCP 设备。
- 每台设备目标速率 10 条/秒。
- 5 秒内发送并确认 24,500 条消息。
- ACK 成功率 100%。
- 实际吞吐量约 4,819 条/秒。
- 平均往返延迟 2.29 ms，P95 5.53 ms，P99 7.17 ms。
- 0 次连接失败，0 条网关侧格式错误。

这是一组本机短时结果，不代表真实工业网络、弱网或 24 小时稳定性。重新测试：

```bash
python3 scripts/benchmark.py --devices 500 --rate 10 --duration 10
```

## 测试

```bash
make test
python3 -m compileall adapters simulator tests
```

端到端测试覆盖：

- TCP 握手、遥测与 ACK。
- 重复序号和错误报文。
- 设备命令下发。
- UDP 遥测。
- 动态插件解码。
- 协议桥虚拟设备。
- Modbus/TCP PLC 模拟、轮询与上报。
- 告警规则、指标和事件 API。

## 安全边界

当前版本定位为作品集与内网原型，不应直接暴露到公网。进入生产环境前至少需要增加：

- TLS/mTLS 与设备证书。
- API 身份认证和 RBAC。
- 配置加密与密钥轮换。
- 持久化消息队列和离线补传。
- 多实例高可用与集中式指标。
- 针对具体厂商协议的模糊测试与安全审计。

## 简历表述原则

只填写自己实际复现过的结果。`benchmark/latest.json` 是可追溯的原始证据；换机器、改参数或修改核心代码后，应重新执行基准测试。
