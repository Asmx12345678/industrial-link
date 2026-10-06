# Validation Record

验证日期：2026-10-03（Asia/Shanghai）

## 构建

```bash
make clean
make -j2
```

结果：C++17 网关和 `vendor_pipe` 动态插件编译成功，编译器开启 `-Wall -Wextra -Wpedantic`，无警告。

## 自动化测试

```bash
make test
```

结果：5 项测试全部通过。

同一测试集还使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 重新编译运行并全部通过；当前 macOS 平台不支持该运行方式下的 LeakSanitizer，因此未把它列为已验证项。

- TCP 握手、遥测、ACK、去重与错误帧。
- HTTP API、命令下发、告警与 SQLite 历史。
- UDP 遥测。
- 动态协议插件。
- 协议桥代理设备。
- Modbus/TCP 模拟 PLC、轮询和上报。
- MQTT 变长字段、QoS 0/1 PUBLISH 解析。

## 标准演示

```bash
make demo
```

10 个设备在 5 秒内发送 100 条并全部收到 ACK，失败数为 0。

## CMake 与 Qt6 客户端

验证日期：2026-10-04。使用项目私有目录中的 CMake 4.4.3、Qt 6.8.3 和 macOS 15.4 SDK，
根工程配置及 Release 构建成功。生成并实际启动了以下 arm64 可执行文件：

- `build/cmake/industrial-gateway`
- `build/cmake/industrial-monitor`
- `build/cmake/libvendor_pipe.so`

客户端连接本机网关后持续显示 20 台虚拟 TCP 设备。60 秒联调共发送并确认 2,400 条消息，
ACK 成功率 100%，平均 RTT 1.299 ms，P95 2.190 ms，无重连与连接失败。

## Docker 全协议联调

验证日期：2026-10-04。Docker Desktop 4.93.0、Engine 29.8.1、Compose 5.5.1。
使用 arm64 Linux 容器完成镜像构建并启动以下 5 个服务：

- C++ 网关（含动态协议插件）
- Eclipse Mosquitto
- MQTT Bridge
- Modbus/TCP PLC 模拟器
- Modbus Bridge

容器均保持 `Up`。REST 健康检查返回 `{"status":"ok"}`；Modbus Bridge 持续产生 PLC
寄存器遥测。宿主机 MQTT 发布器模拟 5 台设备、每台 2 条/秒、持续 5 秒，共 50 条消息
全部经 Broker 和 Bridge 进入网关。网关最终识别 1 台 PLC、5 台 MQTT 设备及 2 个桥接会话，
`malformed_messages=0`。Qt 客户端已连接同一容器网关并显示这些设备。

## 500 连接基准

原始记录：[benchmark/latest.json](../benchmark/latest.json)

```bash
python3 scripts/benchmark.py --devices 500 --rate 10 --duration 5
```

| 指标 | 结果 |
|---|---:|
| TCP 连接 | 500 |
| 已发送/确认 | 24,500 / 24,500 |
| ACK 成功率 | 100% |
| 实际吞吐量 | 4,819.09 条/秒 |
| 平均 RTT | 2.289 ms |
| P95 RTT | 5.528 ms |
| P99 RTT | 7.166 ms |
| 连接失败 | 0 |

## 故障注入

原始记录：[benchmark/fault-latest.json](../benchmark/fault-latest.json)

50 个设备、5 条/秒、5 秒，设置 2% 主动断线和 1% 错误报文：

| 指标 | 结果 |
|---|---:|
| 已发送 | 1,260 |
| 网关已处理 | 1,260 |
| 客户端收到 ACK | 1,239 |
| ACK 观测比例 | 98.33% |
| 成功重连 | 21 |
| 连接失败 | 0 |
| 网关识别错误报文 | 11 |
| P95 RTT | 2.405 ms |

断线发生在 DATA 发出后、ACK 读取前时，网关已经处理该条数据，但客户端无法观察 ACK，因此客户端 ACK 数低于网关处理数。这正是生产系统需要持久化序号和断线补传协议的原因。

## 尚未在本机执行的验证

- 真实 PLC、AGV 或厂商设备：需要硬件、网络环境及对应协议文档。
- 12～24 小时稳定性：当前记录为短时功能和性能验证。

这些限制不影响 TCP/UDP 网关、动态插件、Modbus/TCP 桥、REST API、告警、SQLite 和设备模拟器的本机可运行性，但在简历中必须如实区分“已验证”和“待真实环境验证”。
