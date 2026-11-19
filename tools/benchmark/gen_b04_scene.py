#!/usr/bin/env python3
"""Generate the locked B-04 mixed 300 s Scene IR (pre-measurement pin)."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "contracts/fixtures/b04-mixed/b04-mixed-300s.scene.json"
LOCK = ROOT / "contracts/fixtures/b04-mixed/b04-workload.lock.json"

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
SEGMENTS = (
    ("editorial_open", "EDITORIAL open"),
    ("notebook_proof", "NOTEBOOK proof"),
    ("paper_stage", "PAPER stage"),
    ("editorial_chart", "EDITORIAL chart"),
    ("notebook_summary", "NOTEBOOK summary"),
    ("paper_dissolve", "PAPER dissolve"),
    ("editorial_close", "EDITORIAL close"),
    ("notebook_recall", "NOTEBOOK recall"),
    ("paper_finale", "PAPER finale"),
)


def opacity_track(track_id: str, target: str, start: int, end: int, fade: int = 20):
    mid = min(start + fade, end - 1)
    after = min(end + fade, DURATION - 1)
    return {
        "id": track_id,
        "target_id": target,
        "property": "opacity",
        "interpolation": {"type": "linear"},
        "keyframes": [
            {"frame": start, "value": 0.0},
            {"frame": mid, "value": 1.0},
            {"frame": end - 1, "value": 1.0},
            {"frame": after, "value": 0.0},
        ],
    }


def main() -> int:
    shot_len = DURATION // len(SEGMENTS)
    if shot_len * len(SEGMENTS) != DURATION:
        raise SystemExit("DURATION must divide evenly across mixed segments")

    materials = [
        {
            "id": "ink",
            "shader_feature": "solid-fill",
            "opacity": 1,
            "parameters": {"r": 0.05, "g": 0.05, "b": 0.07, "a": 1},
        },
        {
            "id": "accent",
            "shader_feature": "solid-fill",
            "opacity": 1,
            "parameters": {"r": 0.85, "g": 0.12, "b": 0.08, "a": 1},
        },
        {
            "id": "paper",
            "shader_feature": "solid-fill",
            "opacity": 1,
            "parameters": {"r": 0.96, "g": 0.94, "b": 0.9, "a": 1},
        },
    ]
    geometry = [
        {
            "id": "banner",
            "kind": "path",
            "closed": True,
            "points": [[0, 0], [900, 0], [900, 120], [0, 120]],
        }
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
        },
        {
            "id": "bg_paper",
            "kind": "geometry",
            "layer_order": 1,
            "geometry_id": "banner",
            "material_id": "paper",
            "visible": True,
            "transform": {
                "translate": {"x": 90, "y": 200, "z": 0},
                "scale": {"x": 1.0, "y": 12.0, "z": 1.0},
                "opacity": 1,
            },
        },
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
    shots = []

    for index, (shot_id, label) in enumerate(SEGMENTS):
        start = index * shot_len
        end = start + shot_len
        run_id = f"txt_{shot_id}"
        node_id = f"card_{shot_id}"
        bar_id = f"bar_{shot_id}"
        text_runs.append(
            {
                "id": run_id,
                "text": label,
                "font_asset_id": "font_display",
                "font_size_px": 48,
                "max_width_px": 980,
                "text_role": "headline",
            }
        )
        nodes.append(
            {
                "id": node_id,
                "kind": "text",
                "layer_order": 20 + index,
                "text_run_id": run_id,
                "material_id": "ink",
                "visible": True,
                "transform": {
                    "translate": {"x": 64, "y": 420, "z": 0},
                    "opacity": 0,
                },
            }
        )
        nodes.append(
            {
                "id": bar_id,
                "kind": "geometry",
                "layer_order": 10 + index,
                "geometry_id": "banner",
                "material_id": "accent",
                "visible": True,
                "transform": {
                    "translate": {"x": 64, "y": 640, "z": 0},
                    "scale": {"x": 0.7, "y": 0.35, "z": 1.0},
                    "opacity": 0,
                },
            }
        )
        tracks.append(opacity_track(f"op_{node_id}", node_id, start, end))
        tracks.append(opacity_track(f"op_{bar_id}", bar_id, start, end))
        shots.append(
            {
                "id": shot_id,
                "start_frame": start,
                "duration_frames": shot_len,
                "node_ids": [node_id, bar_id, "bg_paper", "main_cam"],
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
                b"B-04-mixed-300s-v1|seed=42|styles=editorial+notebook+paper"
            ).hexdigest(),
            "style_packages": [
                {"id": "editorial", "version": "1.0.0"},
                {"id": "scientific-notebook", "version": "1.0.0"},
                {"id": "paper-cinema", "version": "1.0.0"},
            ],
        },
    }

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    scene_digest = hashlib.sha256(OUT.read_bytes()).hexdigest()
    lock = {
        "workload_id": "B-04-mixed-300s-v1",
        "scene": "contracts/fixtures/b04-mixed/b04-mixed-300s.scene.json",
        "scene_sha256": scene_digest,
        "asset_lock": ASSET_LOCK[0],
        "asset_lock_sha256": ASSET_LOCK[1],
        "style_bible": STYLE[0],
        "style_sha256": STYLE[1],
        "font_primary_sha256": FONT[1],
        "quality_tier": "mixed-b04-v1",
        "encoder_profile": "h264-high-6mbps-rec709-video-range-no-reorder",
        "duration_frames": DURATION,
        "width": 1080,
        "height": 1920,
        "frame_rate": {"numerator": 30, "denominator": 1},
        "seed_decimal": "42",
        "notes": [
            "Locked before first B-04 mixed reference-series measurement.",
            "Nine editorial/notebook/paper-labeled beats over 9000 frames.",
            "Changing any pinned digest requires a new workload_id baseline.",
        ],
    }
    LOCK.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    print(OUT.relative_to(ROOT), scene_digest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
