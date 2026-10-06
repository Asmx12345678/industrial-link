#!/usr/bin/env python3
"""Launch a clean gateway, run the device simulator, and save a JSON report."""

import argparse
import json
import os
import platform
import signal
import socket
import subprocess
import tempfile
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def free_port(sock_type=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, sock_type) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def get_json(url):
    with urllib.request.urlopen(url, timeout=3) as response:
        return json.loads(response.read().decode())


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devices", type=int, default=500)
    parser.add_argument("--rate", type=float, default=10)
    parser.add_argument("--duration", type=float, default=10)
    parser.add_argument("--disconnect-ratio", type=float, default=0)
    parser.add_argument("--malformed-ratio", type=float, default=0)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    gateway = ROOT / "build" / "industrial-gateway"
    if not gateway.exists():
        raise SystemExit("gateway is not built; run make first")
    tcp_port, udp_port, http_port = free_port(), free_port(socket.SOCK_DGRAM), free_port()
    with tempfile.TemporaryDirectory() as tempdir:
        process = subprocess.Popen([
            str(gateway), "--tcp-port", str(tcp_port), "--udp-port", str(udp_port),
            "--http-port", str(http_port), "--heartbeat", "2", "--timeout", "10",
            "--max-clients", str(max(args.devices + 32, 512)),
            "--event-log", "", "--database", ""
        ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        base_url = f"http://127.0.0.1:{http_port}"
        try:
            for _ in range(100):
                try:
                    if get_json(base_url + "/health")["status"] == "ok":
                        break
                except Exception:
                    time.sleep(0.05)
            else:
                raise RuntimeError("gateway failed to start")
            started = time.monotonic()
            result = subprocess.run([
                "python3", str(ROOT / "simulator" / "simulator.py"),
                "--tcp-port", str(tcp_port), "--devices", str(args.devices),
                "--rate", str(args.rate), "--duration", str(args.duration),
                "--disconnect-ratio", str(args.disconnect_ratio),
                "--malformed-ratio", str(args.malformed_ratio)
            ], cwd=ROOT, capture_output=True, text=True, timeout=args.duration + 30, check=True)
            elapsed = time.monotonic() - started
            simulator_report = json.loads(result.stdout)
            gateway_report = get_json(base_url + "/api/metrics")
            report = {
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "environment": {
                    "platform": platform.platform(),
                    "machine": platform.machine(),
                    "python": platform.python_version(),
                    "cpu_count": os.cpu_count(),
                },
                "configuration": {
                    "devices": args.devices,
                    "rate_per_device": args.rate,
                    "duration_seconds": args.duration,
                    "disconnect_ratio": args.disconnect_ratio,
                    "malformed_ratio": args.malformed_ratio,
                },
                "wall_time_seconds": round(elapsed, 3),
                "simulator": simulator_report,
                "gateway": gateway_report,
            }
            output = args.output
            if output is None:
                output = ROOT / "benchmark" / "latest.json"
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            print(json.dumps(report, ensure_ascii=False, indent=2))
            print(f"\nSaved report to {output}")
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
            if process.stdout:
                process.stdout.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
