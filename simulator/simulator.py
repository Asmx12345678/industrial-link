#!/usr/bin/env python3
"""Concurrent TCP/UDP industrial device simulator for IndustrialLink."""

import argparse
import asyncio
import json
import math
import random
import signal
import socket
import statistics
import time
from dataclasses import dataclass, field
from typing import List, Optional


@dataclass
class Counters:
    sent: int = 0
    acknowledged: int = 0
    commands: int = 0
    reconnects: int = 0
    failures: int = 0
    latencies_ms: List[float] = field(default_factory=list)

    def merge(self, other: "Counters") -> None:
        self.sent += other.sent
        self.acknowledged += other.acknowledged
        self.commands += other.commands
        self.reconnects += other.reconnects
        self.failures += other.failures
        self.latencies_ms.extend(other.latencies_ms)


class TcpDevice:
    def __init__(self, index: int, args: argparse.Namespace, stop: asyncio.Event):
        self.device_id = f"{args.prefix}-{index:04d}"
        self.args = args
        self.stop = stop
        self.stats = Counters()
        self.pending = {}

    async def run(self) -> Counters:
        deadline = time.monotonic() + self.args.duration if self.args.duration else math.inf
        first_connection = True
        while not self.stop.is_set() and time.monotonic() < deadline:
            try:
                reader, writer = await asyncio.open_connection(self.args.host, self.args.tcp_port)
                if not first_connection:
                    self.stats.reconnects += 1
                first_connection = False
                writer.write(f"HELLO {self.device_id} {self.args.device_protocol}\n".encode())
                await writer.drain()
                welcome = await asyncio.wait_for(reader.readline(), timeout=3)
                if not welcome.startswith(b"WELCOME"):
                    raise ConnectionError(f"unexpected handshake: {welcome!r}")
                reader_task = asyncio.create_task(self._read_server(reader, writer))
                try:
                    await self._send_telemetry(writer, deadline)
                finally:
                    reader_task.cancel()
                    await asyncio.gather(reader_task, return_exceptions=True)
                    writer.close()
                    await writer.wait_closed()
            except (ConnectionError, OSError, asyncio.TimeoutError):
                self.stats.failures += 1
                await asyncio.sleep(min(1.0, self.args.reconnect_delay))
        return self.stats

    async def _send_telemetry(self, writer: asyncio.StreamWriter, deadline: float) -> None:
        sequence = 0
        period = 1.0 / max(self.args.rate, 0.1)
        while not self.stop.is_set() and time.monotonic() < deadline:
            sequence += 1
            temperature = 35 + random.random() * 25
            voltage = 23.0 + random.random() * 2
            if self.args.device_protocol.startswith("plugin:"):
                payload = f"TEMP:{temperature:.2f}|VOLT:{voltage:.2f}|STATE:1"
            else:
                payload = f"temperature={temperature:.2f};voltage={voltage:.2f};state=1"
            if random.random() < self.args.malformed_ratio:
                line = "BROKEN malformed-packet\n"
            else:
                line = f"DATA {sequence} {payload}\n"
                self.pending[sequence] = time.monotonic()
                self.stats.sent += 1
            writer.write(line.encode())
            await writer.drain()
            if random.random() < self.args.disconnect_ratio:
                writer.transport.abort()
                return
            await asyncio.sleep(period)

    async def _read_server(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        while not self.stop.is_set():
            line = await reader.readline()
            if not line:
                return
            text = line.decode(errors="replace").strip()
            parts = text.split(maxsplit=2)
            if not parts:
                continue
            if parts[0] == "PING":
                token = parts[1] if len(parts) > 1 else "0"
                writer.write(f"PONG {token}\n".encode())
                await writer.drain()
            elif parts[0] == "ACK" and len(parts) > 1:
                try:
                    sequence = int(parts[1])
                    started = self.pending.pop(sequence, None)
                    if started is not None:
                        self.stats.acknowledged += 1
                        self.stats.latencies_ms.append((time.monotonic() - started) * 1000)
                except ValueError:
                    pass
            elif parts[0] == "COMMAND":
                command_id = parts[1] if len(parts) > 1 else "unknown"
                self.stats.commands += 1
                writer.write(f"ACK {command_id}\n".encode())
                await writer.drain()


async def run_udp(args: argparse.Namespace, stop: asyncio.Event) -> Counters:
    loop = asyncio.get_running_loop()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    destination = (args.host, args.udp_port)
    stats = Counters()
    started = time.monotonic()
    deadline = started + args.duration if args.duration else math.inf
    sequences = [0] * args.devices
    period = 1.0 / max(args.rate, 0.1)
    try:
        while not stop.is_set() and time.monotonic() < deadline:
            batch_started = time.monotonic()
            for index in range(args.devices):
                sequences[index] += 1
                device_id = f"{args.prefix}-{index:04d}"
                payload = f"temperature={35 + random.random() * 25:.2f};state=1"
                message = f"DATA {device_id} {sequences[index]} {payload}\n".encode()
                await loop.sock_sendto(sock, message, destination)
                stats.sent += 1
            elapsed = time.monotonic() - batch_started
            await asyncio.sleep(max(0.0, period - elapsed))
    finally:
        sock.close()
    return stats


def percentile(values: List[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(len(ordered) * fraction) - 1))
    return ordered[index]


async def async_main(args: argparse.Namespace) -> int:
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for name in ("SIGINT", "SIGTERM"):
        if hasattr(signal, name):
            try:
                loop.add_signal_handler(getattr(signal, name), stop.set)
            except NotImplementedError:
                pass
    started = time.monotonic()
    if args.protocol == "udp":
        total = await run_udp(args, stop)
    else:
        devices = [TcpDevice(i, args, stop) for i in range(args.devices)]
        results = await asyncio.gather(*(device.run() for device in devices))
        total = Counters()
        for result in results:
            total.merge(result)
    elapsed = max(time.monotonic() - started, 0.001)
    report = {
        "protocol": args.protocol,
        "devices": args.devices,
        "duration_seconds": round(elapsed, 3),
        "sent": total.sent,
        "acknowledged": total.acknowledged,
        "messages_per_second": round(total.sent / elapsed, 2),
        "ack_ratio": round(total.acknowledged / total.sent, 4) if total.sent else 0,
        "reconnects": total.reconnects,
        "failures": total.failures,
        "latency_ms": {
            "mean": round(statistics.mean(total.latencies_ms), 3) if total.latencies_ms else 0,
            "p50": round(percentile(total.latencies_ms, 0.50), 3),
            "p95": round(percentile(total.latencies_ms, 0.95), 3),
            "p99": round(percentile(total.latencies_ms, 0.99), 3),
        },
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if total.failures == 0 or total.sent > 0 else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--tcp-port", type=int, default=9000)
    parser.add_argument("--udp-port", type=int, default=9001)
    parser.add_argument("--protocol", choices=("tcp", "udp"), default="tcp")
    parser.add_argument("--device-protocol", default="tcp_json",
                        help="HELLO protocol, e.g. tcp_json or plugin:vendor_pipe")
    parser.add_argument("--devices", type=int, default=10)
    parser.add_argument("--rate", type=float, default=2.0, help="messages per device per second")
    parser.add_argument("--duration", type=float, default=10.0, help="0 means run until interrupted")
    parser.add_argument("--prefix", default="sensor")
    parser.add_argument("--disconnect-ratio", type=float, default=0.0)
    parser.add_argument("--malformed-ratio", type=float, default=0.0)
    parser.add_argument("--reconnect-delay", type=float, default=0.25)
    args = parser.parse_args()
    if args.devices < 1 or args.rate <= 0 or args.duration < 0:
        parser.error("devices and rate must be positive; duration must be non-negative")
    for name in ("disconnect_ratio", "malformed_ratio"):
        if not 0 <= getattr(args, name) <= 1:
            parser.error(f"{name.replace('_', '-')} must be between 0 and 1")
    return args


if __name__ == "__main__":
    raise SystemExit(asyncio.run(async_main(parse_args())))

