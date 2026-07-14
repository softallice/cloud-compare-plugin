# SPDX-License-Identifier: GPL-3.0-or-later
"""L4 — semantic interpretation of qBuildingDims geometry.

Division of labour: the CloudCompare plugin *measures* (dims, planes,
openings); this module adds a deterministic, evidence-based *first pass*
of semantic labels, and the MCP prompt (see server.py) lets Claude reason
beyond these rules. Every inference carries a confidence and the evidence
it used, and nothing here claims certainty from a point cloud alone.

Pure functions, no third-party dependencies — unit-testable in isolation.
"""

from __future__ import annotations

import statistics
from typing import Any

# --- opening geometry thresholds (metres; scaled if unit differs) -----------
_DOOR_SILL_MAX = 0.35
_DOOR_W = (0.6, 1.5)
_DOOR_H = (1.8, 2.6)
_DOUBLE_DOOR_W_MIN = 1.5
_GARAGE_W_MIN = 2.2
_GARAGE_H_MIN = 1.9
_STOREFRONT_H_MIN = 2.4
_WINDOW_SILL_MIN = 0.35


def _unit_scale(unit: str) -> float:
    """Factor to convert the given unit into metres for threshold logic."""
    return {"m": 1.0, "mm": 0.001, "cm": 0.01, "ft": 0.3048, "in": 0.0254}.get(unit, 1.0)


def classify_opening(op: dict[str, Any], scale: float) -> dict[str, Any]:
    """Refines a door/window into a specific semantic type with evidence."""
    w = op.get("width", 0.0) * scale
    h = op.get("height", 0.0) * scale
    sill = op.get("sill_height", 0.0) * scale
    base_conf = float(op.get("confidence", 0.5))

    reasons: list[str] = []
    at_ground = sill <= _DOOR_SILL_MAX

    if at_ground and w >= _GARAGE_W_MIN and h >= _GARAGE_H_MIN:
        semantic = "garage_or_overhead_door"
        reasons.append(f"ground-level, wide ({w:.2f} m) and tall ({h:.2f} m)")
    elif at_ground and w >= _DOUBLE_DOOR_W_MIN and h >= _DOOR_H[0]:
        semantic = "double_door_or_entrance"
        reasons.append(f"ground-level, wide ({w:.2f} m)")
    elif at_ground and _DOOR_W[0] <= w <= _DOOR_W[1] and _DOOR_H[0] <= h <= _DOOR_H[1]:
        semantic = "door"
        reasons.append(f"ground-level, door-sized ({w:.2f}x{h:.2f} m)")
    elif sill <= 0.6 and h >= _STOREFRONT_H_MIN and w >= 1.5:
        semantic = "storefront_or_curtain_glazing"
        reasons.append(f"low sill ({sill:.2f} m), large glazing ({w:.2f}x{h:.2f} m)")
    elif sill >= _WINDOW_SILL_MIN:
        semantic = "window"
        reasons.append(f"raised sill ({sill:.2f} m)")
    else:
        semantic = op.get("type", "unknown")
        reasons.append("ambiguous geometry")

    # Confidence blends the plugin's frame-occupancy with rule specificity.
    rule_conf = 0.85 if semantic not in ("unknown", op.get("type")) else 0.5
    confidence = round(min(1.0, 0.5 * base_conf + 0.5 * rule_conf), 3)

    return {
        "wall_index": op.get("wall_index"),
        "semantic": semantic,
        "width": op.get("width"),
        "height": op.get("height"),
        "sill_height": op.get("sill_height"),
        "confidence": confidence,
        "evidence": reasons,
    }


def detect_fenestration(classified: list[dict[str, Any]], scale: float) -> list[dict[str, Any]]:
    """Finds regular window rows/bands per wall (repeated size + sill band)."""
    patterns: list[dict[str, Any]] = []
    by_wall: dict[Any, list[dict[str, Any]]] = {}
    for c in classified:
        if c["semantic"] in ("window", "storefront_or_curtain_glazing"):
            by_wall.setdefault(c["wall_index"], []).append(c)

    for wall_index, wins in by_wall.items():
        if len(wins) < 3:
            continue
        widths = [w.get("width", 0.0) for w in wins]
        sills = [w.get("sill_height", 0.0) for w in wins]
        w_mean = statistics.mean(widths)
        w_cv = (statistics.pstdev(widths) / w_mean) if w_mean else 1.0
        sill_bands = len({round(s * scale, 0) for s in sills})  # ~1 m buckets

        if w_cv < 0.25:
            patterns.append({
                "wall_index": wall_index,
                "kind": "regular_fenestration",
                "window_count": len(wins),
                "sill_bands": sill_bands,
                "evidence": [
                    f"{len(wins)} similarly-sized windows (width CV={w_cv:.2f})",
                    f"{sill_bands} distinct sill band(s) — consistent with per-storey rows",
                ],
                "confidence": round(min(0.9, 0.5 + 0.1 * len(wins)), 3),
            })
    return patterns


def infer_typology(analysis: dict[str, Any], classified: list[dict[str, Any]],
                   scale: float) -> dict[str, Any]:
    """Low-confidence building-use hypothesis from coarse geometry. Flagged."""
    dims = analysis.get("dimensions", {})
    length = dims.get("length", 0.0) * scale
    width = dims.get("width", 0.0) * scale
    height = dims.get("height", 0.0) * scale
    footprint_area = length * width
    storeys = analysis.get("planes_analysis", {}).get("storey_count", 0)

    semantics = [c["semantic"] for c in classified]
    has_storefront = "storefront_or_curtain_glazing" in semantics
    has_garage = "garage_or_overhead_door" in semantics
    window_count = semantics.count("window")
    opening_density = len(classified) / footprint_area if footprint_area else 0.0

    evidence = [
        f"footprint ~{footprint_area:.0f} m^2, height ~{height:.1f} m, storeys~{storeys}",
        f"{len(classified)} openings (windows={window_count}), density={opening_density:.3f}/m^2",
    ]

    if has_storefront:
        hypothesis, conf = "commercial_retail", 0.5
        evidence.append("ground-level storefront glazing present")
    elif has_garage and footprint_area > 200 and window_count <= 2:
        hypothesis, conf = "industrial_warehouse", 0.45
        evidence.append("large footprint, few windows, overhead door")
    elif storeys and storeys >= 1 and window_count >= 3:
        hypothesis, conf = "residential", 0.4
        evidence.append("multiple regular windows, moderate footprint")
    else:
        hypothesis, conf = "undetermined", 0.2

    return {
        "hypothesis": hypothesis,
        "confidence": conf,
        "evidence": evidence,
        "caveat": "Typology is a low-confidence hypothesis from geometry alone; "
                  "confirm against imagery / context.",
    }


def interpret(analysis: dict[str, Any]) -> dict[str, Any]:
    """Full L4 pass over a qBuildingDims analysis JSON object."""
    unit = analysis.get("unit", "m")
    scale = _unit_scale(unit)

    raw_openings = analysis.get("openings", [])
    classified = [classify_opening(o, scale) for o in raw_openings]
    fenestration = detect_fenestration(classified, scale)
    typology = infer_typology(analysis, classified, scale)

    counts: dict[str, int] = {}
    for c in classified:
        counts[c["semantic"]] = counts.get(c["semantic"], 0) + 1

    dims = analysis.get("dimensions", {})
    storeys = analysis.get("planes_analysis", {}).get("storey_count", 0)
    summary = (
        f"Building ~{dims.get('length', 0):.1f} x {dims.get('width', 0):.1f} x "
        f"{dims.get('height', 0):.1f} {unit}, ~{storeys} storey(s). "
        f"Openings: " + (", ".join(f"{n}x {k}" for k, n in counts.items()) or "none detected") + ". "
        f"Likely use: {typology['hypothesis']} (confidence {typology['confidence']})."
    )

    return {
        "ok": True,
        "unit": unit,
        "summary": summary,
        "opening_semantics": classified,
        "opening_counts": counts,
        "fenestration_patterns": fenestration,
        "typology": typology,
        "notes": [
            "Semantic labels are heuristic first-pass; use the interpret prompt "
            "for deeper reasoning.",
            "Accuracy depends on scan density, occlusion and glazing returns.",
        ],
    }
