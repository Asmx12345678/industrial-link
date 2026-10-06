#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

QT_VERSION="${QT_VERSION:-6.8.3}"
CMAKE_BIN="$ROOT_DIR/.tools/venv/bin/cmake"
QT_ROOT="$ROOT_DIR/.tools/Qt/${QT_VERSION}/macos"

if [[ ! -x "$CMAKE_BIN" || ! -d "$QT_ROOT" ]]; then
  echo "Local CMake/Qt not found; run ./scripts/bootstrap_macos_qt.sh first" >&2
  exit 1
fi

CMAKE_ARGS=(
  -S .
  -B build/cmake
  -DCMAKE_BUILD_TYPE=Release
  "-DCMAKE_PREFIX_PATH=$QT_ROOT"
)

if [[ -d /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk ]]; then
  CMAKE_ARGS+=("-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk")
fi

"$CMAKE_BIN" "${CMAKE_ARGS[@]}"
"$CMAKE_BIN" --build build/cmake --parallel
