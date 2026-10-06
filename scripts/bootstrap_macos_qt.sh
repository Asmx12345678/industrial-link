#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

QT_VERSION="${QT_VERSION:-6.8.3}"
python3 -m venv .tools/venv
.tools/venv/bin/python -m pip install --upgrade pip
.tools/venv/bin/pip install cmake aqtinstall

if [[ ! -d ".tools/Qt/${QT_VERSION}/macos" ]]; then
  .tools/venv/bin/aqt install-qt mac desktop "$QT_VERSION" clang_64 -O .tools/Qt
fi

echo "CMake: $(.tools/venv/bin/cmake --version | head -1)"
echo "Qt: $ROOT_DIR/.tools/Qt/${QT_VERSION}/macos"
