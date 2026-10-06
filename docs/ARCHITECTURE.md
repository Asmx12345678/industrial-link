# Architecture

## 设计目标

IndustrialLink 解决三个问题：异构协议接入、连接可靠性和可观测性。核心网关不依赖大型框架，便于展示 Linux Socket、事件循环、协议边界和故障恢复机制。

## 组件

### Gateway Reactor

网关在单个事件循环中监听三个入口：

- TCP 设备端口。
- UDP 设备端口。
- HTTP 管理端口。

TCP 连接使用非阻塞 Socket。事件循环通过 `select` 找到可读描述符，然后解析完整换行帧。每个 Session 保存设备 ID、协议、状态、最后心跳、序号、消息量和接收缓冲区。

该模型避免“一连接一线程”的栈内存和调度开销。当前短时基准已验证 500 个并发连接；在 Linux 上可将 `select` 替换为 epoll，以突破 `FD_SETSIZE` 限制。

### Session Lifecycle

```text
ACCEPTED → CONNECTING → ONLINE → TIMEOUT/OFFLINE
                       ↑   │
                       └───┘ PING/PONG
```

设备必须先发送 `HELLO`。网关定期发送 `PING`，收到任何有效流量都会更新 `last_seen`。超时连接被关闭，设备重连后相同 ID 的旧连接会被替换。

### Normalization

TCP/UDP、插件和协议桥最终都转换成：

```text
device_id + protocol + sequence + payload
```

这使监控端不必了解底层协议。MQTT 与 Modbus/TCP 使用独立桥进程，桥以 `PROXY_DATA` 在一个长连接上代理多个虚拟设备。

### Plugin ABI

动态插件使用 C ABI，避免不同 C++ 编译器产生的 ABI 不兼容。插件只承担厂商载荷解析，不管理 Socket，因此可以独立测试和热替换进程版本。

### Reliability

- 接收缓冲区上限为 1 MiB，防止无换行数据耗尽内存。
- 序号小于或等于已接收序号时返回 `DUP`。
- 协议桥和设备模拟器采用指数退避重连。
- 事件日志逐行写入 JSONL，单行损坏不会破坏整个文件。
- API 请求大小限制为 64 KiB，设备命令限制为 4 KiB。

### Alarm Rules

当前内置两条可演示规则：

- `temperature > 80`：高温告警。
- `voltage < 20 || voltage > 28`：电压越界。

规则同时支持 `key=value` 和简单 JSON 数值字段。生产版本应将规则配置化，并增加迟滞、持续时间和告警合并。

## 数据流

```text
设备发送 DATA
  → Session 读取并组帧
  → 验证握手与序号
  → 可选插件解码
  → 标准遥测事件
  → 告警规则
  → JSONL 日志和内存事件窗口
  → REST API / Qt 客户端
  → 返回 ACK
```

## 扩展点

- 将 Reactor 抽象为 kqueue/epoll/IOCP。
- 以线程池执行 CPU 密集型解码和规则。
- SQLite/PostgreSQL 历史存储。
- WebSocket 或 SSE 实时推送。
- TLS/mTLS、RBAC 和设备证书。
- Prometheus 指标与 OpenTelemetry Trace。

