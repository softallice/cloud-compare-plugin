#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Places plugin/ into a CloudCompare source tree and registers it in CMake.
# Idempotent. Usage: scripts/integrate.sh <path-to-CloudCompare-source>
set -euo pipefail

CC_SRC="${1:?usage: integrate.sh <CloudCompare source dir>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

[ -d "$CC_SRC/plugins/core/Standard" ] || {
	echo "ERROR: '$CC_SRC' does not look like a CloudCompare source tree" >&2
	exit 1
}

dest="$CC_SRC/plugins/core/Standard/qBuildingDims"
echo "Copying plugin -> $dest"
rm -rf "$dest"
cp -r "$ROOT/plugin" "$dest"

cml="$CC_SRC/plugins/core/Standard/CMakeLists.txt"
if grep -q "qBuildingDims" "$cml"; then
	echo "CMake already registers qBuildingDims"
else
	echo "Registering add_subdirectory( qBuildingDims )"
	sed -i '1i add_subdirectory( qBuildingDims )' "$cml"
fi
echo "Integration done. Enable with -DPLUGIN_STANDARD_QBUILDINGDIMS=ON"
