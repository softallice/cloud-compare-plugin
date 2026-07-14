#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Fast smoke test of the parts that DON'T need a full CloudCompare build:
#   - DxfWriter (pure C++/std): compile + run + sanity-check DXF output
#   - interpret.py (L4): pure-Python semantic pass self-test
# Runs in seconds; use before pushing.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== [1/2] DxfWriter compile + run =="
cat > "$TMP/test_dxf.cpp" <<'CPP'
#include "DxfWriter.h"
#include <cstdio>
int main() {
    DxfWriter d;
    d.addPolyline("FOOTPRINT", {{0,0},{24,0},{24,12},{0,12}}, true);
    d.addAlignedDim("DIMENSIONS", 0,0, 24,0, -2.0, "24.00 m", 0.5);
    d.addText("TEXT", 12,6, 0.5, "PLAN");
    return d.write("out.dxf") ? 0 : 1;
}
CPP
g++ -std=c++17 -Wall -I"$ROOT/plugin/include" \
    "$TMP/test_dxf.cpp" "$ROOT/plugin/src/DxfWriter.cpp" -o "$TMP/test_dxf"
( cd "$TMP" && ./test_dxf && grep -q "ENTITIES" out.dxf && grep -q "EOF" out.dxf )
echo "   OK: DxfWriter produced a valid DXF"

echo "== [2/2] interpret.py (L4) self-test =="
python3 - "$ROOT/mcp" <<'PY'
import sys, json
sys.path.insert(0, sys.argv[1])
import interpret
a = {"unit":"m","dimensions":{"length":24,"width":12,"height":9},
     "planes_analysis":{"storey_count":3},
     "openings":[
        {"wall_index":0,"type":"door","width":0.95,"height":2.1,"sill_height":0.02,"confidence":0.8},
        {"wall_index":0,"type":"window","width":1.2,"height":1.4,"sill_height":0.95,"confidence":0.7},
        {"wall_index":0,"type":"window","width":1.2,"height":1.4,"sill_height":0.95,"confidence":0.7},
        {"wall_index":0,"type":"window","width":1.25,"height":1.4,"sill_height":3.9,"confidence":0.7}]}
out = interpret.interpret(a)
assert out["ok"] and out["opening_counts"].get("door")==1, out
assert any(p["kind"]=="regular_fenestration" for p in out["fenestration_patterns"]), out
print("   OK:", out["summary"])
PY

echo "== standalone verification PASSED =="
