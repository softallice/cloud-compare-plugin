#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds CloudCompare + the qBuildingDims plugin on Linux to verify the C++
# compiles/links against real CloudCompare headers (catches API mismatches).
#
# Dependencies (Ubuntu 24.04):
#   sudo apt install -y build-essential cmake git \
#     qt6-base-dev qt6-tools-dev qt6-tools-dev-tools libqt6opengl6-dev \
#     libqt6svg6-dev libgdal-dev
#
# Usage: scripts/build-linux.sh [CloudCompare-src-dir] [CC-git-ref]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC_SRC="${1:-$HOME/CloudCompare}"
CC_REF="${2:-master}"
JOBS="$(nproc 2>/dev/null || echo 4)"

command -v cmake >/dev/null || { echo "cmake not found — install deps (see header)"; exit 1; }

if [ ! -d "$CC_SRC/.git" ]; then
	echo "== Cloning CloudCompare ($CC_REF) into $CC_SRC =="
	git clone --recursive https://github.com/CloudCompare/CloudCompare.git "$CC_SRC"
	git -C "$CC_SRC" checkout "$CC_REF"
	git -C "$CC_SRC" submodule update --init --recursive
else
	echo "== Updating submodules in $CC_SRC =="
	git -C "$CC_SRC" submodule update --init --recursive
fi

"$ROOT/scripts/integrate.sh" "$CC_SRC"

echo "== Configuring =="
cmake -S "$CC_SRC" -B "$CC_SRC/build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DPLUGIN_STANDARD_QBUILDINGDIMS=ON

echo "== Building (jobs=$JOBS) =="
cmake --build "$CC_SRC/build" --config Release -j "$JOBS"

echo "== BUILD OK. Binaries under $CC_SRC/build =="
