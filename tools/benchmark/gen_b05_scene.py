#!/usr/bin/env python3
"""Generate the locked B-05 worst-accepted 300 s Scene IR (pre-measurement pin)."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "contracts/fixtures/b05-worst-accepted/b05-worst-accepted-300s.scene.json"
LOCK = ROOT / "contracts/fixtures/b05-worst-accepted/b05-workload.lock.json"
TIER = ROOT / "contracts/fixtures/b05-worst-accepted/complexity-tier.json"

FONT = (
    "assets/editorial/fonts/NotoSans-Regular.ttf",
    "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5",
)
STYLE = (
    "contracts/fixtures/style-bible/positive/editorial.style.json",
    "c8b5052920f44f0d2fdf8280202614f73b54c23982d28052ac79a2456d916a1c",
)
ASSET_LOCK = (
    "assets/editorial/editorial.lock.json",
    "809028d61f6263751fceaa80409e8c632768200b6a61c18f6c98ae578557c62a",
)

DURATION = 9000
TIER_LIMITS = {
    "max_nodes": 2048,
    "max_node_depth": 24,
    "max_assets": 256,
    "max_geometry": 1024,
    "max_text_runs": 512,
    "max_text_codepoints": 50000,
    "max_draw_items": 1536,
    "max_mask_materials": 128,
    "max_pixel_layers": 512,
    "max_animation_tracks": 2048,
    "max_keyframes": 32768,
    "max_shots": 128,
    "max_canvas_width_px": 1080,
    "max_canvas_height_px": 1920,
}

PANEL_COUNT = 48
TEXT_COUNT = 48
SHOT_COUNT = 15  # divides 9000 frames evenly (600 frames/shot)


def main() -> int:
    if DURATION % SHOT_COUNT != 0:
        raise SystemExit("DURATION must divide across shots")
    shot_len = DURATION // SHOT_COUNT

    materials = [
        {
            "id": f"mat_{i}",
            "shader_feature": "solid-fill",
            "opacity": 1,
            "parameters": {
                "r": 0.1 + (i % 5) * 0.08,
                "g": 0.12 + (i % 3) * 0.05,
                "b": 0.2 + (i % 4) * 0.05,
                "a": 1,
            },
        }
        for i in range(12)
    ]
    geometry = [
        {
            "id": f"geo_{i}",
            "kind": "path",
            "closed": True,
            "points": [
                [0, 0],
                [100 + (i % 7) * 6, 0],
                [100 + (i % 7) * 6, 70 + (i % 5) * 4],
                [0, 70 + (i % 5) * 4],
            ],
        }
        for i in range(PANEL_COUNT)
    ]
    assets = [
        {
            "id": "font_display",
            "uri": FONT[0],
            "sha256": FONT[1],
            "media_type": "font/ttf",
            "color_profile": "untagged",
        }
    ]
    text_runs = []
    nodes = [
        {
            "id": "main_cam",
            "kind": "camera",
            "layer_order": 0,
            "camera_id": "cam0",
            "visible": True,
        }
    ]
    cameras = [
        {
            "id": "cam0",
            "projection": "orthographic",
            "near": 0.1,
            "far": 1000,
            "transform": {"translate": {"x": 0, "y": 0, "z": 10}, "opacity": 1},
        }
    ]
    tracks = []
    panel_ids: list[str] = []
    label_ids: list[str] = []

    for i in range(PANEL_COUNT):
        node_id = f"panel_{i}"
        panel_ids.append(node_id)
        nodes.append(
            {
                "id": node_id,
                "kind": "geometry",
                "layer_order": 5 + i,
                "geometry_id": f"geo_{i}",
                "material_id": f"mat_{i % 12}",
                "visible": True,
                "transform": {
                    "translate": {
                        "x": 40.0 + (i % 8) * 120.0,
                        "y": 180.0 + (i // 8) * 130.0,
                        "z": 0.0,
                    },
                    "opacity": 0.4,
                },
            }
        )
        tracks.append(
            {
                "id": f"op_{node_id}",
                "target_id": node_id,
                "property": "opacity",
                "interpolation": {"type": "linear"},
                "keyframes": [
                    {"frame": 0, "value": 0.35},
                    {"frame": 4500, "value": 1.0 if i % 3 else 0.55},
                    {"frame": DURATION - 1, "value": 0.4},
                ],
            }
        )

    for i in range(TEXT_COUNT):
        run_id = f"run_{i}"
        node_id = f"label_{i}"
        label_ids.append(node_id)
        text_runs.append(
            {
                "id": run_id,
                "text": f"W{i:02d} worst-accepted panel label",
                "font_asset_id": "font_display",
                "font_size_px": 18 + (i % 4),
                "max_width_px": 320,
                "text_role": "body",
            }
        )
        nodes.append(
            {
                "id": node_id,
                "kind": "text",
                "layer_order": 100 + i,
                "text_run_id": run_id,
                "material_id": "mat_0",
                "visible": True,
                "transform": {
                    "translate": {
                        "x": 48.0 + (i % 6) * 160.0,
                        "y": 1600.0 + (i // 6) * 28.0,
                        "z": 0.0,
                    },
                    "opacity": 1,
                },
            }
        )

    shots = []
    for s in range(SHOT_COUNT):
        start = s * shot_len
        member_panels = [panel_ids[i] for i in range(s * 3, min(s * 3 + 12, PANEL_COUNT))]
        member_labels = [label_ids[i] for i in range(s * 3, min(s * 3 + 12, TEXT_COUNT))]
        shots.append(
            {
                "id": f"stress_{s:02d}",
                "start_frame": start,
                "duration_frames": shot_len,
                "node_ids": ["main_cam", *member_panels, *member_labels],
                "camera_node_id": "main_cam",
            }
        )

    scene = {
        "schema_version": "1.0.0",
        "compiler_version": "0.1.0",
        "canvas": {"width": 1080, "height": 1920, "color": "rec709-sdr"},
        "frame_rate": {"numerator": 30, "denominator": 1},
        "duration_frames": DURATION,
        "seed": 42,
        "assets": assets,
        "materials": materials,
        "geometry": geometry,
        "text_runs": text_runs,
        "cameras": cameras,
        "rigs": [],
        "nodes": nodes,
        "animation_tracks": tracks,
        "shots": shots,
        "transition_spans": [],
        "provenance": {
            "project_sha256": hashlib.sha256(
                b"B-05-worst-accepted-300s-v1|seed=42|tier=worst-accepted-b05-v1"
            ).hexdigest(),
            "style_packages": [{"id": "editorial", "version": "1.0.0"}],
        },
    }

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    scene_digest = hashlib.sha256(OUT.read_bytes()).hexdigest()

    tier_doc = {
        "tier_id": "worst-accepted-b05-v1",
        "inherits_structural_caps_from": "p3-foundation-draft",
        "limits": TIER_LIMITS,
        "notes": [
            "Published V1 worst-accepted complexity ceilings.",
            "Scenes above these limits must fail closed before large allocations.",
        ],
    }
    TIER.write_text(json.dumps(tier_doc, indent=2) + "\n", encoding="utf-8")

    lock = {
        "workload_id": "B-05-worst-accepted-300s-v1",
        "scene": "contracts/fixtures/b05-worst-accepted/b05-worst-accepted-300s.scene.json",
        "scene_sha256": scene_digest,
        "asset_lock": ASSET_LOCK[0],
        "asset_lock_sha256": ASSET_LOCK[1],
        "style_bible": STYLE[0],
        "style_sha256": STYLE[1],
        "font_primary_sha256": FONT[1],
        "quality_tier": "worst-accepted-b05-v1",
        "complexity_tier": "worst-accepted-b05-v1",
        "complexity_tier_doc": "contracts/fixtures/b05-worst-accepted/complexity-tier.json",
        "encoder_profile": "h264-high-6mbps-rec709-video-range-no-reorder",
        "duration_frames": DURATION,
        "width": 1080,
        "height": 1920,
        "frame_rate": {"numerator": 30, "denominator": 1},
        "seed_decimal": "42",
        "notes": [
            "Locked before first B-05 worst-accepted measurement.",
            "Complexity ceilings are explicit in complexity-tier.json.",
            "Changing any pinned digest requires a new workload_id baseline.",
        ],
    }
    LOCK.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    print(OUT.relative_to(ROOT), scene_digest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
