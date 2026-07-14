# SPDX-License-Identifier: GPL-3.0-or-later
"""MCP bridge server for the qBuildingDims CloudCompare plugin.

Exposes CloudCompare's headless ``-BUILDING_DIMS`` command as MCP tools so
Claude Desktop (or any MCP client) can extract building dimensions from a
point cloud and export a DXF footprint through natural-language requests.

The plugin does the real work; this server only shells out to the
CloudCompare CLI and relays the JSON it produces.

Run:
    CLOUDCOMPARE_BIN="C:/Program Files/CloudCompare/CloudCompare.exe" \
        python server.py
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from mcp.server.fastmcp import FastMCP

import interpret as _interpret

mcp = FastMCP("cloudcompare-building-dims")

# Windows default install path is the common case for Claude Desktop users.
_DEFAULT_WINDOWS = r"C:\Program Files\CloudCompare\CloudCompare.exe"


def _resolve_binary() -> str:
    """Locate the CloudCompare executable.

    Order: $CLOUDCOMPARE_BIN, PATH lookup, Windows default install path.
    """
    env = os.environ.get("CLOUDCOMPARE_BIN")
    if env:
        if not Path(env).exists():
            raise FileNotFoundError(f"CLOUDCOMPARE_BIN does not exist: {env}")
        return env

    found = shutil.which("CloudCompare") or shutil.which("cloudcompare")
    if found:
        return found

    if Path(_DEFAULT_WINDOWS).exists():
        return _DEFAULT_WINDOWS

    raise FileNotFoundError(
        "CloudCompare executable not found. Set the CLOUDCOMPARE_BIN "
        "environment variable to its full path."
    )


def _run_cli(args: list[str], timeout: int) -> subprocess.CompletedProcess[str]:
    binary = _resolve_binary()
    cmd = [binary, "-SILENT", *args]
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


@mcp.tool()
def extract_building_dims(
    cloud_path: str,
    method: str = "OBB",
    unit: str = "m",
    dxf_out: str | None = None,
    detect_planes: bool = False,
    detect_openings: bool = False,
    plan_out: str | None = None,
    elevation_out: str | None = None,
    timeout_seconds: int = 300,
) -> dict[str, Any]:
    """Extract building dimensions (length, width, height) from a point cloud.

    Args:
        cloud_path: Path to the point cloud file (.las, .laz, .e57, .ply, .bin, ...).
        method: "OBB" (oriented bounding box, default) or "AABB" (axis-aligned).
        unit: Unit label carried into the result (default "m").
        dxf_out: Optional path to write the footprint as a DXF file.
        detect_planes: Also run wall/floor/roof plane + storey analysis (L2).
        detect_openings: Also detect windows/doors as framed voids in walls (L3).
            Implies plane detection. Adds an "openings" array to the result.
        plan_out: Optional path to write a top-down PLAN drawing (DXF, with
            footprint, wall segments, and dimension lines). Implies plane detection.
        elevation_out: Optional path to write ELEVATION drawings (DXF, one framed
            rectangle per wall with width/height dimension lines). Implies planes.
        timeout_seconds: Max seconds to wait for CloudCompare.

    Returns:
        Parsed dimension result: dimensions{length,width,height}, footprint,
        center, global_shift, point_count, warnings, and — when requested —
        planes_analysis{plane_count, planes[], storey_count, storey_height}.
    """
    src = Path(cloud_path)
    if not src.exists():
        return {"ok": False, "error": f"File not found: {cloud_path}"}

    method_u = method.upper()
    if method_u not in ("OBB", "AABB"):
        return {"ok": False, "error": f"Invalid method: {method} (use OBB or AABB)"}

    with tempfile.TemporaryDirectory() as tmp:
        json_out = Path(tmp) / "dims.json"
        args = [
            "-O", str(src),
            "-BUILDING_DIMS",
            "-METHOD", method_u,
            "-UNIT", unit,
            "-JSON", str(json_out),
        ]
        if dxf_out:
            args += ["-DXF", str(dxf_out)]
        if detect_planes or plan_out or elevation_out:
            args += ["-PLANES"]
        if detect_openings:
            args += ["-OPENINGS"]
        if plan_out:
            args += ["-PLAN", str(plan_out)]
        if elevation_out:
            args += ["-ELEV", str(elevation_out)]

        try:
            proc = _run_cli(args, timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            return {"ok": False, "error": f"CloudCompare timed out after {timeout_seconds}s"}
        except FileNotFoundError as exc:
            return {"ok": False, "error": str(exc)}

        if not json_out.exists():
            return {
                "ok": False,
                "error": "CloudCompare produced no JSON output.",
                "returncode": proc.returncode,
                "stdout_tail": proc.stdout[-2000:],
                "stderr_tail": proc.stderr[-2000:],
            }

        result = json.loads(json_out.read_text(encoding="utf-8"))

    if dxf_out:
        result["dxf_path"] = dxf_out
    if plan_out:
        result["plan_path"] = plan_out
    if elevation_out:
        result["elevation_path"] = elevation_out
    return result


@mcp.tool()
def interpret_building(
    cloud_path: str,
    unit: str = "m",
    timeout_seconds: int = 300,
) -> dict[str, Any]:
    """L4 — extract geometry then apply semantic interpretation.

    Runs the full plugin pipeline (dims + planes + openings) on the cloud,
    then classifies openings (door/window/garage/storefront), detects
    regular fenestration rows, and offers a low-confidence building-use
    hypothesis. Every inference carries confidence + evidence.

    For deeper reasoning beyond these rules, use the `interpret_building`
    prompt with the returned JSON.
    """
    analysis = extract_building_dims(
        cloud_path,
        unit=unit,
        detect_planes=True,
        detect_openings=True,
        timeout_seconds=timeout_seconds,
    )
    if not analysis.get("ok", False):
        return analysis

    interpretation = _interpret.interpret(analysis)
    return {"ok": True, "measurements": analysis, "interpretation": interpretation}


@mcp.prompt()
def interpret_building_prompt(analysis_json: str) -> str:
    """Reasoning scaffold for Claude to interpret a building analysis JSON."""
    return (
        "You are given geometric measurements of a building extracted from a "
        "point cloud by the qBuildingDims CloudCompare plugin (dimensions, wall/"
        "floor/roof planes, storeys, and detected openings with sizes and sill "
        "heights).\n\n"
        "Interpret it as an architect would, and STRICTLY:\n"
        "1. Distinguish what is measured (high confidence) from what is inferred.\n"
        "2. For each opening, reason about its likely function (door, window, "
        "garage, entrance, storefront) from width/height/sill and neighbours.\n"
        "3. Identify patterns: window rows per storey, symmetry, primary entrance.\n"
        "4. Offer a building-use hypothesis ONLY with explicit confidence and the "
        "evidence for it; state what would confirm or refute it.\n"
        "5. Flag scan-quality risks (occlusion, glazing, low density) that could "
        "distort the geometry. Never overclaim from a point cloud alone.\n\n"
        f"Analysis JSON:\n{analysis_json}\n"
    )


@mcp.tool()
def cloudcompare_info() -> dict[str, Any]:
    """Report which CloudCompare binary the bridge will use and its version."""
    try:
        binary = _resolve_binary()
    except FileNotFoundError as exc:
        return {"ok": False, "error": str(exc)}

    try:
        proc = _run_cli(["-HELP"], timeout=30)
        version_hint = (proc.stdout or proc.stderr)[:500]
    except Exception as exc:  # noqa: BLE001 - surface any launch failure to the agent
        return {"ok": False, "binary": binary, "error": str(exc)}

    return {"ok": True, "binary": binary, "output_head": version_hint}


if __name__ == "__main__":
    try:
        mcp.run()
    except KeyboardInterrupt:
        sys.exit(0)
