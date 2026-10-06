import json
import os
import signal
import socket
import sqlite3
import subprocess
import tempfile
import time
import unittest
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GATEWAY = ROOT / "build" / "industrial-gateway"
PLUGIN = ROOT / "build" / ("libvendor_pipe.dylib" if os.uname().sysname == "Darwin" else "libvendor_pipe.so")


def free_port(sock_type=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, sock_type) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def read_json(url, data=None):
    headers = {"Content-Type": "application/json"}
    request = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(request, timeout=3) as response:
        return json.loads(response.read().decode())


def recv_line(sock, expected_prefix, timeout=3):
    sock.settimeout(timeout)
    buffer = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        while b"\n" not in buffer:
            buffer += sock.recv(4096)
        line, buffer = buffer.split(b"\n", 1)
        decoded = line.decode()
        if decoded.startswith("PING "):
            sock.sendall(("PONG " + decoded.split(maxsplit=1)[1] + "\n").encode())
            continue
        if decoded.startswith(expected_prefix):
            return decoded
    raise AssertionError(f"did not receive line beginning with {expected_prefix!r}")


class GatewayIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not GATEWAY.exists():
            raise unittest.SkipTest("gateway binary not built; run make first")
        cls.tcp_port = free_port()
        cls.udp_port = free_port(socket.SOCK_DGRAM)
        cls.http_port = free_port()
        cls.tempdir = tempfile.TemporaryDirectory()
        cls.database = Path(cls.tempdir.name) / "industrial-link.db"
        command = [
            str(GATEWAY),
            "--tcp-port", str(cls.tcp_port),
            "--udp-port", str(cls.udp_port),
            "--http-port", str(cls.http_port),
            "--heartbeat", "1",
            "--timeout", "5",
            "--event-log", str(Path(cls.tempdir.name) / "events.jsonl"),
            "--database", str(cls.database),
            "--plugin", str(PLUGIN),
        ]
        cls.process = subprocess.Popen(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        cls.base_url = f"http://127.0.0.1:{cls.http_port}"
        for _ in range(60):
            if cls.process.poll() is not None:
                output = cls.process.stdout.read() if cls.process.stdout else ""
                raise RuntimeError(f"gateway exited early:\n{output}")
            try:
                if read_json(cls.base_url + "/health")["status"] == "ok":
                    break
            except Exception:
                time.sleep(0.05)
        else:
            raise RuntimeError("gateway did not become healthy")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "process") and cls.process.poll() is None:
            cls.process.send_signal(signal.SIGTERM)
            try:
                cls.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                cls.process.kill()
        if hasattr(cls, "process") and cls.process.stdout:
            cls.process.stdout.close()
        if hasattr(cls, "tempdir"):
            cls.tempdir.cleanup()

    def test_end_to_end_protocols_plugin_api_and_command(self):
        device = socket.create_connection(("127.0.0.1", self.tcp_port), timeout=3)
        self.addCleanup(device.close)
        device.sendall(b"HELLO sensor-test tcp_json\n")
        self.assertTrue(recv_line(device, "WELCOME").startswith("WELCOME"))

        device.sendall(b"DATA 1 temperature=42.5;voltage=24.1\n")
        self.assertEqual(recv_line(device, "ACK 1"), "ACK 1")
        device.sendall(b"DATA 1 temperature=99\n")
        self.assertEqual(recv_line(device, "DUP 1"), "DUP 1")
        device.sendall(b"DATA 2 temperature=85.0;voltage=24.0\n")
        self.assertEqual(recv_line(device, "ACK 2"), "ACK 2")
        device.sendall(b"BROKEN payload\n")
        self.assertEqual(recv_line(device, "ERROR"), "ERROR unknown_message")

        devices = read_json(self.base_url + "/api/devices")["devices"]
        current = next(item for item in devices if item["id"] == "sensor-test")
        self.assertEqual(current["protocol"], "tcp_json")
        self.assertEqual(current["last_sequence"], 2)

        payload = json.dumps({"device_id": "sensor-test", "command": "set_rate=5"}).encode()
        self.assertEqual(read_json(self.base_url + "/api/command", payload)["status"], "sent")
        command = recv_line(device, "COMMAND")
        _, command_id, body = command.split(maxsplit=2)
        self.assertEqual(body, "set_rate=5")
        device.sendall(f"ACK {command_id}\n".encode())

        plugin_device = socket.create_connection(("127.0.0.1", self.tcp_port), timeout=3)
        plugin_device.sendall(b"HELLO vendor-001 plugin:vendor_pipe\n")
        self.assertTrue(recv_line(plugin_device, "WELCOME"))
        plugin_device.sendall(b"DATA 7 TEMP:45.2|VOLT:23.9|STATE:1\n")
        self.assertEqual(recv_line(plugin_device, "ACK 7"), "ACK 7")
        plugin_device.close()

        bridge = socket.create_connection(("127.0.0.1", self.tcp_port), timeout=3)
        bridge.sendall(b"HELLO mqtt-bridge-test bridge:mqtt\n")
        self.assertTrue(recv_line(bridge, "WELCOME"))
        bridge.sendall(b"PROXY_DATA mqtt-sensor-01 mqtt 4 {\"temperature\":38.2}\n")
        self.assertEqual(recv_line(bridge, "ACK_PROXY"), "ACK_PROXY mqtt-sensor-01 4")
        bridge.close()

        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.settimeout(2)
        udp.sendto(b"DATA udp-001 3 temperature=31.5;state=1\n",
                   ("127.0.0.1", self.udp_port))
        self.assertEqual(udp.recvfrom(1024)[0].strip(), b"ACK 3")
        udp.close()

        time.sleep(0.1)
        metrics = read_json(self.base_url + "/api/metrics")
        self.assertGreaterEqual(metrics["messages_received"], 4)
        self.assertEqual(metrics["duplicate_messages"], 1)
        self.assertGreaterEqual(metrics["malformed_messages"], 1)
        self.assertEqual(metrics["commands_sent"], 1)
        self.assertGreaterEqual(metrics["alarms_triggered"], 1)

        plugins = read_json(self.base_url + "/api/plugins")["plugins"]
        self.assertIn("vendor_pipe@1.0.0", plugins)
        events = read_json(self.base_url + "/api/events")["events"]
        details = "\n".join(event["detail"] for event in events)
        self.assertIn("TEMP=45.2;VOLT=23.9;STATE=1", details)
        self.assertTrue(any(event["device_id"] == "udp-001" for event in events))
        self.assertTrue(any(event["device_id"] == "mqtt-sensor-01" for event in events))
        self.assertTrue(any(event["type"] == "alarm" for event in events))
        history = read_json(self.base_url + "/api/history?device_id=sensor-test&limit=20")["events"]
        self.assertTrue(history)
        self.assertTrue(all(item["device_id"] == "sensor-test" for item in history))
        with sqlite3.connect(self.database) as database:
            persisted = database.execute("SELECT COUNT(*) FROM events").fetchone()[0]
            alarms = database.execute("SELECT COUNT(*) FROM events WHERE type='alarm'").fetchone()[0]
        self.assertGreaterEqual(persisted, len(events))
        self.assertGreaterEqual(alarms, 1)

    def test_modbus_bridge_with_simulated_plc(self):
        modbus_port = free_port()
        config_path = Path(self.tempdir.name) / "modbus-test.json"
        config_path.write_text(json.dumps({"devices": [{
            "id": "plc-test", "host": "127.0.0.1", "port": modbus_port,
            "unit": 1, "start": 0, "count": 4, "interval": 0.05,
            "register_names": ["temperature_x10", "voltage_x10", "speed_rpm", "state"]
        }]}), encoding="utf-8")
        simulator = subprocess.Popen([
            "python3", str(ROOT / "simulator" / "modbus_server.py"),
            "--port", str(modbus_port)
        ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        bridge = subprocess.Popen([
            "python3", str(ROOT / "adapters" / "modbus_bridge.py"),
            "--gateway-port", str(self.tcp_port),
            "--config", str(config_path), "--interval", "0.05",
            "--bridge-id", "modbus-bridge-test"
        ], cwd=ROOT / "adapters", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            for _ in range(100):
                devices = read_json(self.base_url + "/api/devices")["devices"]
                plc = next((item for item in devices if item["id"] == "plc-test"), None)
                if plc and plc["messages"] >= 2:
                    break
                if simulator.poll() is not None or bridge.poll() is not None:
                    sim_output = simulator.stdout.read() if simulator.stdout else ""
                    bridge_output = bridge.stdout.read() if bridge.stdout else ""
                    self.fail(f"protocol process exited early\nsimulator={sim_output}\nbridge={bridge_output}")
                time.sleep(0.05)
            else:
                self.fail("Modbus bridge did not publish PLC telemetry")
            self.assertEqual(plc["protocol"], "modbus_tcp")
            events = read_json(self.base_url + "/api/events")["events"]
            detail = "\n".join(item["detail"] for item in events if item["device_id"] == "plc-test")
            self.assertIn("temperature_x10=", detail)
            self.assertIn("speed_rpm=", detail)
        finally:
            for process in (bridge, simulator):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        process.kill()
                if process.stdout:
                    process.stdout.close()


if __name__ == "__main__":
    unittest.main()
