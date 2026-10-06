#!/usr/bin/env python3
"""Dependency-free MQTT 3.1.1 telemetry publisher for the demo broker."""

import argparse
import asyncio
import json
import random
import struct
import time


def mqtt_string(value):
    encoded = value.encode()
    return struct.pack("!H", len(encoded)) + encoded


def remaining_length(value):
    output = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value:
            digit |= 0x80
        output.append(digit)
        if not value:
            return bytes(output)


async def read_packet(reader):
    first = (await reader.readexactly(1))[0]
    multiplier, length = 1, 0
    while True:
        digit = (await reader.readexactly(1))[0]
        length += (digit & 0x7F) * multiplier
        if not digit & 0x80:
            break
        multiplier *= 128
    return first, await reader.readexactly(length)


async def run(args):
    reader, writer = await asyncio.open_connection(args.host, args.port)
    client_id = f"industrial-link-publisher-{random.randint(1000, 9999)}"
    variable = mqtt_string("MQTT") + bytes([4, 2]) + struct.pack("!H", 30)
    payload = mqtt_string(client_id)
    writer.write(bytes([0x10]) + remaining_length(len(variable) + len(payload)) + variable + payload)
    await writer.drain()
    packet, body = await read_packet(reader)
    if packet >> 4 != 2 or body[1] != 0:
        raise ConnectionError("broker rejected publisher")
    started = time.monotonic()
    sequences = [0] * args.devices
    while time.monotonic() - started < args.duration:
        for index in range(args.devices):
            sequences[index] += 1
            topic = f"industrial/mqtt-{index:04d}/telemetry"
            message = json.dumps({
                "sequence": sequences[index],
                "temperature": round(35 + random.random() * 20, 2),
                "voltage": round(23 + random.random() * 2, 2),
            }, separators=(",", ":"))
            publish = mqtt_string(topic) + message.encode()
            writer.write(bytes([0x30]) + remaining_length(len(publish)) + publish)
        await writer.drain()
        await asyncio.sleep(1 / args.rate)
    writer.write(b"\xe0\x00")
    await writer.drain()
    writer.close()
    await writer.wait_closed()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--devices", type=int, default=5)
    parser.add_argument("--rate", type=float, default=2)
    parser.add_argument("--duration", type=float, default=10)
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(asyncio.run(run(parse_args())))

