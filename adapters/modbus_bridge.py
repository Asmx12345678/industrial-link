#!/usr/bin/env python3
"""Modbus/TCP register poller and IndustrialLink protocol bridge."""

import argparse
import asyncio
import json
import struct
from pathlib import Path

from gateway_bridge import GatewayBridge


async def read_holding_registers(reader, writer, transaction_id, unit, start, count):
    pdu = struct.pack("!BHH", 3, start, count)
    request = struct.pack("!HHH", transaction_id, 0, len(pdu) + 1) + bytes([unit]) + pdu
    writer.write(request)
    await writer.drain()
    header = await asyncio.wait_for(reader.readexactly(7), timeout=3)
    response_transaction, protocol, length, response_unit = struct.unpack("!HHHB", header)
    if response_transaction != transaction_id or protocol != 0 or response_unit != unit:
        raise ValueError("invalid Modbus/TCP response header")
    body = await asyncio.wait_for(reader.readexactly(length - 1), timeout=3)
    function = body[0]
    if function & 0x80:
        raise ValueError(f"Modbus exception {body[1] if len(body) > 1 else -1}")
    if function != 3 or len(body) < 2 or body[1] != count * 2:
        raise ValueError("invalid read holding registers response")
    return list(struct.unpack("!" + "H" * count, body[2:]))


async def poll_device(device, bridge, interval):
    sequence = 0
    transaction = 0
    delay = 0.5
    while True:
        writer = None
        try:
            reader, writer = await asyncio.open_connection(device["host"], device.get("port", 502))
            delay = 0.5
            while True:
                transaction = (transaction + 1) % 65535 or 1
                registers = await read_holding_registers(
                    reader, writer, transaction, device.get("unit", 1),
                    device.get("start", 0), device.get("count", 4))
                sequence += 1
                names = device.get("register_names", [])
                fields = []
                for index, value in enumerate(registers):
                    name = names[index] if index < len(names) else f"register_{device.get('start', 0) + index}"
                    fields.append(f"{name}={value}")
                await bridge.publish(device["id"], "modbus_tcp", sequence, ";".join(fields))
                await asyncio.sleep(device.get("interval", interval))
        except asyncio.CancelledError:
            raise
        except (ConnectionError, OSError, asyncio.TimeoutError, ValueError) as error:
            print(json.dumps({"event": "modbus_reconnect", "device": device["id"],
                              "error": str(error)}, ensure_ascii=False))
            await asyncio.sleep(delay)
            delay = min(delay * 2, 10)
        finally:
            if writer:
                writer.close()
                await writer.wait_closed()


async def run(args):
    devices = json.loads(Path(args.config).read_text(encoding="utf-8"))["devices"]
    bridge = GatewayBridge(args.bridge_id, "modbus", args.gateway_host, args.gateway_port)
    await bridge.start()
    try:
        await asyncio.gather(*(poll_device(device, bridge, args.interval) for device in devices))
    finally:
        await bridge.close()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gateway-host", default="127.0.0.1")
    parser.add_argument("--gateway-port", type=int, default=9000)
    parser.add_argument("--config", default="config/modbus_devices.json")
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--bridge-id", default="modbus-bridge")
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(asyncio.run(run(parse_args())))

