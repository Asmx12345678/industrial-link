"""Shared resilient connection from protocol bridges to IndustrialLink."""

import asyncio
import contextlib
from typing import Optional


class GatewayBridge:
    def __init__(self, bridge_id: str, protocol: str, host: str, port: int):
        self.bridge_id = bridge_id
        self.protocol = protocol
        self.host = host
        self.port = port
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self.lock = asyncio.Lock()
        self.connected = asyncio.Event()
        self._runner: Optional[asyncio.Task] = None

    async def start(self) -> None:
        self._runner = asyncio.create_task(self._connection_loop())
        await asyncio.wait_for(self.connected.wait(), timeout=10)

    async def close(self) -> None:
        if self._runner:
            self._runner.cancel()
            await asyncio.gather(self._runner, return_exceptions=True)
        if self.writer:
            self.writer.close()
            await self.writer.wait_closed()

    async def publish(self, device_id: str, device_protocol: str,
                      sequence: int, payload: str) -> None:
        line = f"PROXY_DATA {device_id} {device_protocol} {sequence} {payload}\n"
        for attempt in range(2):
            await self.connected.wait()
            async with self.lock:
                try:
                    assert self.writer is not None
                    self.writer.write(line.encode())
                    await self.writer.drain()
                    return
                except (ConnectionError, OSError):
                    self.connected.clear()
            if attempt == 0:
                await asyncio.sleep(0.2)
        raise ConnectionError("gateway bridge is unavailable")

    async def _connection_loop(self) -> None:
        delay = 0.2
        while True:
            try:
                self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
                self.writer.write(f"HELLO {self.bridge_id} bridge:{self.protocol}\n".encode())
                await self.writer.drain()
                response = await asyncio.wait_for(self.reader.readline(), timeout=3)
                if not response.startswith(b"WELCOME"):
                    raise ConnectionError(f"gateway rejected bridge: {response!r}")
                self.connected.set()
                delay = 0.2
                await self._read_loop()
            except asyncio.CancelledError:
                raise
            except (ConnectionError, OSError, asyncio.TimeoutError):
                self.connected.clear()
                if self.writer:
                    self.writer.close()
                    with contextlib.suppress(Exception):
                        await self.writer.wait_closed()
                self.writer = None
                await asyncio.sleep(delay)
                delay = min(delay * 2, 5)

    async def _read_loop(self) -> None:
        assert self.reader is not None and self.writer is not None
        while True:
            line = await self.reader.readline()
            if not line:
                raise ConnectionError("gateway closed bridge connection")
            text = line.decode(errors="replace").strip()
            if text.startswith("PING "):
                self.writer.write(("PONG " + text.split(maxsplit=1)[1] + "\n").encode())
                await self.writer.drain()

