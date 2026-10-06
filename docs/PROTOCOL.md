# Device and Bridge Protocol

TCP 协议为 UTF-8 换行帧，每条消息以 `\n` 结束。单条设备命令不能包含换行。

## 设备握手

```text
Device → Gateway: HELLO sensor-001 tcp_json
Gateway → Device: WELCOME heartbeat=5
```

必须先完成握手。使用插件时协议名为 `plugin:<name>`：

```text
HELLO vendor-001 plugin:vendor_pipe
```

## 遥测

```text
Device → Gateway: DATA 42 temperature=48.2;voltage=23.8;state=1
Gateway → Device: ACK 42
```

序号必须单调递增。重复或倒退时：

```text
Gateway → Device: DUP 42
```

## 心跳

```text
Gateway → Device: PING 1791014400123
Device → Gateway: PONG 1791014400123
```

## 指令

```text
Gateway → Device: COMMAND 1791014400456 set_rate=5
Device → Gateway: ACK 1791014400456
```

## 协议桥

桥进程先注册：

```text
HELLO mqtt-bridge bridge:mqtt
```

然后代理虚拟设备：

```text
PROXY_DATA mqtt-device-01 mqtt 7 {"temperature":38.2}
ACK_PROXY mqtt-device-01 7
```

只有 `bridge:*` 会话可以发送 `PROXY_DATA`。

## UDP

UDP 数据报自带设备 ID：

```text
DATA udp-001 9 temperature=41.2;state=1
```

网关向数据报来源地址返回：

```text
ACK 9
```

UDP 在线状态由最后数据报时间维护。

## 错误

```text
ERROR hello_required
ERROR invalid_device_id
ERROR invalid_sequence
ERROR unknown_message
ERROR plugin_decode
ERROR bridge_protocol_required
```

