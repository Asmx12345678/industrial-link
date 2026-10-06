#!/usr/bin/env python3
"""Small deterministic Modbus/TCP server for demos and fault testing."""

import argparse
import asyncio
import contextlib
import math
import struct
import time


class ModbusServer:
    def __init__(self):
        self.started = time.monotonic()
        self.requests = 0

    def registers(self):
        elapsed = time.monotonic() - self.started
        return [
            int(420 + math.sin(elapsed / 5) * 40),
            int(240 + math.sin(elapsed / 7) * 5),
            int(1200 + math.sin(elapsed / 3) * 200),
            1,
        ]

    async def handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        try:
            while True:
                header = await reader.readexactly(7)
                transaction, protocol, length, unit = struct.unpack("!HHHB", header)
                body = await reader.readexactly(length - 1)
                self.requests += 1
                if protocol != 0 or not body:
                    return
                function = body[0]
                if function != 3 or len(body) != 5:
                    response_pdu = bytes([function | 0x80, 1])
                else:
                    start, count = struct.unpack("!HH", body[1:])
                    values = self.registers()
                    if count < 1 or count > 125 or start + count > len(values):
                        response_pdu = bytes([0x83, 2])
                    else:
                        selected = values[start:start + count]
                        response_pdu = bytes([3, count * 2]) + struct.pack("!" + "H" * count, *selected)
                response = struct.pack("!HHHB", transaction, 0, len(response_pdu) + 1, unit) + response_pdu
                writer.write(response)
                await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            writer.close()
            with contextlib.suppress(Exception):
                await writer.wait_closed()


async def run(args):
    implementation = ModbusServer()
    server = await asyncio.start_server(implementation.handle, args.host, args.port)
    addresses = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
    print(f"Modbus simulator listening on {addresses}", flush=True)
    async with server:
        if args.duration:
            await asyncio.sleep(args.duration)
        else:
            await server.serve_forever()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1502)
    parser.add_argument("--duration", type=float, default=0)
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(asyncio.run(run(parse_args())))

