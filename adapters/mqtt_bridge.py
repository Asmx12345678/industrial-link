#!/usr/bin/env python3
"""Minimal MQTT 3.1.1 subscriber that forwards telemetry to IndustrialLink.

No third-party MQTT package is required. QoS 0 and QoS 1 PUBLISH packets are
accepted; QoS 1 packets are acknowledged with PUBACK.
"""

import argparse
import asyncio
import contextlib
import json
import random
import struct
from collections import defaultdict

from gateway_bridge import GatewayBridge


def mqtt_string(value: str) -> bytes:
    encoded = value.encode()
    return struct.pack("!H", len(encoded)) + encoded


def remaining_length(value: int) -> bytes:
    output = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value:
            digit |= 0x80
        output.append(digit)
        if not value:
            return bytes(output)


async def read_packet(reader: asyncio.StreamReader):
    first = (await reader.readexactly(1))[0]
    multiplier, length = 1, 0
    for _ in range(4):
        digit = (await reader.readexactly(1))[0]
        length += (digit & 0x7F) * multiplier
        if not digit & 0x80:
            break
        multiplier *= 128
    else:
        raise ValueError("invalid MQTT remaining length")
    return first, await reader.readexactly(length)


def connect_packet(client_id: str, keepalive: int) -> bytes:
    variable = mqtt_string("MQTT") + bytes([4, 2]) + struct.pack("!H", keepalive)
    payload = mqtt_string(client_id)
    return bytes([0x10]) + remaining_length(len(variable) + len(payload)) + variable + payload


def subscribe_packet(topic: str, packet_id: int = 1) -> bytes:
    body = struct.pack("!H", packet_id) + mqtt_string(topic) + bytes([1])
    return bytes([0x82]) + remaining_length(len(body)) + body


def parse_publish(first: int, body: bytes):
    topic_length = struct.unpack("!H", body[:2])[0]
    cursor = 2
    topic = body[cursor:cursor + topic_length].decode(errors="replace")
    cursor += topic_length
    qos = (first >> 1) & 0x03
    packet_id = None
    if qos:
        packet_id = struct.unpack("!H", body[cursor:cursor + 2])[0]
        cursor += 2
    return topic, body[cursor:].decode(errors="replace"), qos, packet_id


def device_from_topic(topic: str) -> str:
    parts = topic.split("/")
    if len(parts) >= 3 and parts[-1] == "telemetry":
        return parts[-2]
    return topic.replace("/", "-")


async def run(args):
    bridge = GatewayBridge(args.bridge_id, "mqtt", args.gateway_host, args.gateway_port)
    await bridge.start()
    sequence = defaultdict(int)
    delay = 0.5
    try:
        while True:
            writer = None
            try:
                reader, writer = await asyncio.open_connection(args.broker_host, args.broker_port)
                writer.write(connect_packet(args.client_id, args.keepalive))
                await writer.drain()
                packet_type, body = await asyncio.wait_for(read_packet(reader), timeout=5)
                if packet_type >> 4 != 2 or len(body) < 2 or body[1] != 0:
                    raise ConnectionError(f"MQTT CONNACK rejected: {body!r}")
                writer.write(subscribe_packet(args.topic))
                await writer.drain()
                packet_type, body = await asyncio.wait_for(read_packet(reader), timeout=5)
                if packet_type >> 4 != 9:
                    raise ConnectionError("MQTT SUBACK missing")
                delay = 0.5
                while True:
                    try:
                        first, body = await asyncio.wait_for(read_packet(reader), timeout=args.keepalive / 2)
                    except asyncio.TimeoutError:
                        writer.write(b"\xc0\x00")
                        await writer.drain()
                        continue
                    kind = first >> 4
                    if kind == 3:
                        topic, payload, qos, packet_id = parse_publish(first, body)
                        device_id = device_from_topic(topic)
                        sequence[device_id] += 1
                        await bridge.publish(device_id, "mqtt", sequence[device_id], payload)
                        if qos == 1 and packet_id is not None:
                            writer.write(b"\x40\x02" + struct.pack("!H", packet_id))
                            await writer.drain()
                    elif kind in (13, 9):
                        continue
            except asyncio.CancelledError:
                raise
            except (ConnectionError, OSError, asyncio.IncompleteReadError, ValueError) as error:
                print(json.dumps({"event": "mqtt_reconnect", "error": str(error)}, ensure_ascii=False))
                await asyncio.sleep(delay + random.random() * 0.2)
                delay = min(delay * 2, 10)
            finally:
                if writer:
                    writer.close()
                    with contextlib.suppress(Exception):
                        await writer.wait_closed()
    finally:
        await bridge.close()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gateway-host", default="127.0.0.1")
    parser.add_argument("--gateway-port", type=int, default=9000)
    parser.add_argument("--broker-host", default="127.0.0.1")
    parser.add_argument("--broker-port", type=int, default=1883)
    parser.add_argument("--topic", default="industrial/+/telemetry")
    parser.add_argument("--client-id", default="industrial-link-mqtt-bridge")
    parser.add_argument("--bridge-id", default="mqtt-bridge")
    parser.add_argument("--keepalive", type=int, default=30)
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(asyncio.run(run(parse_args())))

