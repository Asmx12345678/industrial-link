#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

PLUGIN_EXT="so"
if [[ "$(uname -s)" == "Darwin" ]]; then
  PLUGIN_EXT="dylib"
fi

mkdir -p data
./build/industrial-gateway \
  --plugin "./build/libvendor_pipe.${PLUGIN_EXT}" \
  --event-log data/events.jsonl >data/gateway.log 2>&1 &
GATEWAY_PID=$!
cleanup() {
  kill "$GATEWAY_PID" 2>/dev/null || true
  wait "$GATEWAY_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

for _ in {1..50}; do
  if curl -fsS http://127.0.0.1:8080/health >/dev/null; then
    break
  fi
  sleep 0.1
done

python3 simulator/simulator.py --devices 10 --rate 2 --duration 5
curl -fsS http://127.0.0.1:8080/api/metrics
echo
echo "Dashboard: http://127.0.0.1:8080"

