#!/usr/bin/env python3
"""Generate the locked B-02 notebook 60 s Pythagoras explainer Scene IR."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "contracts/fixtures/b02-notebook/b02-pythagoras-explainer.scene.json"

FONT_DISPLAY = (
    "assets/notebook/fonts/NotoSans-Regular.ttf",
    "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5",
)
FONT_MATH = (
    "assets/notebook/fonts/DejaVuSans.ttf",
    "7da195a74c55bef988d0d48f9508bd5d849425c1770dba5d7bfc6ce9ed848954",
)
PAPER = (
    "assets/notebook/textures/paper-fiber.png",
    "df61a66ed37a77444b3895527bce67b42640690cc9da8719a4a151129bcb9169",
)
HATCH = (
    "assets/notebook/patterns/sketch-hatch.png",
    "30c9d2165d3384b1a1f7c694cc796cf63ad482046b9ff3dc680ca05ed70fc50d",
)


def rect(w: float, h: float) -> list[list[float]]:
    return [[0, 0], [w, 0], [w, h], [0, h]]


def opacity_track(track_id: str, target: str, start: int, end: int, fade: int = 12):
    mid = min(start + fade, end)
    return {
        "id": track_id,
        "target_id": target,
        "property": "opacity",
        "interpolation": {"type": "linear"},
        "keyframes": [
            {"frame": start, "value": 0.0},
            {"frame": mid, "value": 1.0},
            {"frame": end, "value": 1.0},
        ],
    }


def text_run(run_id: str, text: str, font: str, size: int, width: int, role: str):
    return {
        "id": run_id,
        "text": text,
        "font_asset_id": font,
        "font_size_px": size,
        "max_width_px": width,
        "text_role": role,
    }


def text_node(node_id: str, run_id: str, material: str, x: float, y: float, layer: int):
    return {
        "id": node_id,
        "kind": "text",
        "layer_order": layer,
        "text_run_id": run_id,
        "material_id": material,
        "visible": True,
        "transform": {
            "translate": {"x": x, "y": y, "z": 0},
            "opacity": 0,
        },
    }


def geom_node(node_id: str, geom: str, material: str, x: float, y: float, layer: int):
    return {
        "id": node_id,
        "kind": "geometry",
        "layer_order": layer,
        "geometry_id": geom,
        "material_id": material,
        "visible": True,
        "transform": {
            "translate": {"x": x, "y": y, "z": 0},
            "opacity": 0,
            "scale": {"x": 1, "y": 1, "z": 1},
        },
    }


def image_node(node_id: str, asset: str, material: str, x: float, y: float, layer: int):
    return {
        "id": node_id,
        "kind": "image",
        "layer_order": layer,
        "material_id": material,
        "visible": True,
        "transform": {
            "translate": {"x": x, "y": y, "z": 0},
            "opacity": 0,
        },
        "asset_id": asset,
    }


def main() -> int:
    # Scaled 2× from the 900-frame notebook template (problem→summary).
    shots = [
        ("problem_statement", 0, 258),
        ("construction", 258, 386),
        ("derivation", 644, 516),
        ("visual_intuition", 1160, 384),
        ("summary", 1544, 256),
    ]
    assert sum(d for _, _, d in shots) == 1800

    scene = {
        "schema_version": "1.0.0",
        "compiler_version": "0.1.0",
        "canvas": {"width": 1080, "height": 1920, "color": "rec709-sdr"},
        "frame_rate": {"numerator": 30, "denominator": 1},
        "duration_frames": 1800,
        "seed": 42,
        "assets": [
            {
                "id": "font_display",
                "uri": FONT_DISPLAY[0],
                "sha256": FONT_DISPLAY[1],
                "media_type": "font/ttf",
                "color_profile": "untagged",
            },
            {
                "id": "font_math",
                "uri": FONT_MATH[0],
                "sha256": FONT_MATH[1],
                "media_type": "font/ttf",
                "color_profile": "untagged",
            },
            {
                "id": "paper_fiber",
                "uri": PAPER[0],
                "sha256": PAPER[1],
                "media_type": "image/png",
                "color_profile": "untagged",
            },
            {
                "id": "sketch_hatch",
                "uri": HATCH[0],
                "sha256": HATCH[1],
                "media_type": "image/png",
                "color_profile": "untagged",
            },
        ],
        "materials": [
            {
                "id": "ink",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.12, "g": 0.14, "b": 0.18, "a": 1},
            },
            {
                "id": "accent",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.18, "g": 0.42, "b": 0.62, "a": 1},
            },
            {
                "id": "paper",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.96, "g": 0.94, "b": 0.88, "a": 1},
            },
            {
                "id": "ink_light",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.35, "g": 0.38, "b": 0.42, "a": 1},
            },
            {
                "id": "highlight",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.95, "g": 0.82, "b": 0.25, "a": 1},
            },
        ],
        "geometry": [
            {"id": "leg_a", "kind": "path", "closed": True, "points": rect(40, 280)},
            {"id": "leg_b", "kind": "path", "closed": True, "points": rect(360, 40)},
            {
                "id": "hyp",
                "kind": "path",
                "closed": True,
                "points": [[0, 0], [360, 0], [360, 24], [0, 24]],
            },
            {"id": "sq_a", "kind": "path", "closed": True, "points": rect(120, 120)},
            {"id": "sq_b", "kind": "path", "closed": True, "points": rect(200, 200)},
            {"id": "sq_c", "kind": "path", "closed": True, "points": rect(240, 240)},
            {"id": "pin_dot", "kind": "path", "closed": True, "points": rect(28, 28)},
            {
                "id": "tape_strip",
                "kind": "path",
                "closed": True,
                "points": rect(48, 14),
            },
            {
                "id": "ink_bar",
                "kind": "path",
                "closed": True,
                "points": rect(594, 20),
            },
        ],
        "text_runs": [
            text_run(
                "txt_ps_title",
                "Pythagorean relation",
                "font_display",
                56,
                980,
                "headline",
            ),
            text_run(
                "txt_ps_prompt",
                "Relate the sides of a right triangle.",
                "font_display",
                36,
                980,
                "body",
            ),
            text_run(
                "txt_co_title",
                "Pythagorean relation",
                "font_display",
                48,
                980,
                "headline",
            ),
            text_run(
                "txt_co_step1",
                "Draw right triangle ABC with right angle at C.",
                "font_display",
                32,
                980,
                "body",
            ),
            text_run(
                "txt_co_step2",
                "Mark legs a, b and hypotenuse c.",
                "font_display",
                32,
                980,
                "body",
            ),
            text_run(
                "txt_de_title",
                "Pythagorean relation",
                "font_display",
                48,
                980,
                "headline",
            ),
            text_run(
                "txt_de_eq1",
                "a^2 + b^2 = c^2",
                "font_math",
                64,
                980,
                "headline",
            ),
            text_run(
                "txt_de_eq2",
                "c = sqrt(a^2 + b^2)",
                "font_math",
                48,
                980,
                "body",
            ),
            text_run(
                "txt_vi_title",
                "Pythagorean relation",
                "font_display",
                44,
                980,
                "headline",
            ),
            text_run(
                "txt_vi_note",
                "Squares on the legs fill the square on the hypotenuse.",
                "font_display",
                28,
                980,
                "body",
            ),
            text_run(
                "txt_vi_a",
                "A",
                "font_math",
                28,
                80,
                "body",
            ),
            text_run(
                "txt_vi_b",
                "B",
                "font_math",
                28,
                80,
                "body",
            ),
            text_run(
                "txt_vi_c",
                "C",
                "font_math",
                28,
                80,
                "body",
            ),
            text_run(
                "txt_su_title",
                "Pythagorean relation",
                "font_display",
                48,
                980,
                "headline",
            ),
            text_run(
                "txt_su_claim",
                "In a right triangle, a^2 + b^2 = c^2.",
                "font_math",
                40,
                980,
                "headline",
            ),
            text_run(
                "txt_su_b1",
                "Works in Euclidean plane geometry.",
                "font_display",
                28,
                980,
                "body",
            ),
            text_run(
                "txt_su_b2",
                "Foundation for distance in R^2.",
                "font_display",
                28,
                980,
                "body",
            ),
        ],
        "cameras": [
            {
                "id": "cam0",
                "projection": "orthographic",
                "near": 0.1,
                "far": 1000,
                "transform": {
                    "translate": {"x": 0, "y": 0, "z": 10},
                    "opacity": 1,
                },
            }
        ],
        "rigs": [],
        "nodes": [
            {
                "id": "main_cam",
                "kind": "camera",
                "layer_order": 0,
                "camera_id": "cam0",
                "visible": True,
            },
            image_node("paper_bg", "paper_fiber", "paper", 0, 0, 1),
            text_node("ps_title", "txt_ps_title", "ink", 48, 80, 20),
            text_node("ps_prompt", "txt_ps_prompt", "ink_light", 48, 220, 19),
            geom_node("ps_pin", "pin_dot", "accent", 1044, 8, 22),
            text_node("co_title", "txt_co_title", "ink", 48, 64, 20),
            text_node("co_step1", "txt_co_step1", "ink", 48, 180, 19),
            text_node("co_step2", "txt_co_step2", "ink", 48, 1010, 19),
            image_node("co_hatch", "sketch_hatch", "ink_light", 120, 420, 8),
            text_node("de_title", "txt_de_title", "ink", 48, 64, 20),
            text_node("de_eq1", "txt_de_eq1", "ink", 64, 320, 21),
            text_node("de_eq2", "txt_de_eq2", "accent", 64, 720, 21),
            geom_node("de_highlight", "ink_bar", "highlight", 48, 1892, 18),
            text_node("vi_title", "txt_vi_title", "ink", 48, 48, 20),
            geom_node("vi_leg_a", "leg_a", "ink", 220, 420, 12),
            geom_node("vi_leg_b", "leg_b", "ink", 260, 660, 12),
            geom_node("vi_hyp", "hyp", "accent", 220, 420, 13),
            geom_node("vi_sq_a", "sq_a", "ink_light", 80, 420, 10),
            geom_node("vi_sq_b", "sq_b", "ink_light", 260, 700, 10),
            geom_node("vi_sq_c", "sq_c", "accent", 620, 360, 10),
            text_node("vi_a", "txt_vi_a", "ink", 200, 700, 22),
            text_node("vi_b", "txt_vi_b", "ink", 420, 700, 22),
            text_node("vi_c", "txt_vi_c", "ink", 220, 380, 22),
            text_node("vi_note", "txt_vi_note", "ink_light", 48, 1200, 19),
            geom_node("vi_tape", "tape_strip", "highlight", 378, 40, 22),
            text_node("su_title", "txt_su_title", "ink", 48, 64, 20),
            text_node("su_claim", "txt_su_claim", "ink", 48, 220, 21),
            text_node("su_b1", "txt_su_b1", "ink_light", 48, 420, 19),
            text_node("su_b2", "txt_su_b2", "ink_light", 48, 500, 19),
        ],
        "animation_tracks": [
            {
                "id": "cam_push",
                "target_id": "main_cam",
                "property": "translate_z",
                "interpolation": {"type": "linear"},
                "keyframes": [
                    {"frame": 0, "value": 10},
                    {"frame": 900, "value": 8.5},
                    {"frame": 1799, "value": 10},
                ],
            },
            opacity_track("paper_bg_in", "paper_bg", 0, 1799, fade=1),
        ],
        "shots": [],
        "transition_spans": [],
        "provenance": {
            "project_sha256": hashlib.sha256(
                b"b02-pythagoras-explainer-v1"
            ).hexdigest(),
            "style_packages": [
                {"id": "scientific-notebook", "version": "1.0.0"}
            ],
        },
    }

    shot_nodes = {
        "problem_statement": [
            "ps_title",
            "ps_prompt",
            "ps_pin",
            "paper_bg",
            "main_cam",
        ],
        "construction": [
            "co_title",
            "co_step1",
            "co_step2",
            "co_hatch",
            "paper_bg",
            "main_cam",
        ],
        "derivation": [
            "de_title",
            "de_eq1",
            "de_eq2",
            "de_highlight",
            "paper_bg",
            "main_cam",
        ],
        "visual_intuition": [
            "vi_title",
            "vi_leg_a",
            "vi_leg_b",
            "vi_hyp",
            "vi_sq_a",
            "vi_sq_b",
            "vi_sq_c",
            "vi_a",
            "vi_b",
            "vi_c",
            "vi_note",
            "vi_tape",
            "paper_bg",
            "main_cam",
        ],
        "summary": [
            "su_title",
            "su_claim",
            "su_b1",
            "su_b2",
            "paper_bg",
            "main_cam",
        ],
    }

    for name, start, duration in shots:
        end = start + duration - 1
        scene["shots"].append(
            {
                "id": name,
                "start_frame": start,
                "duration_frames": duration,
                "node_ids": shot_nodes[name],
                "camera_node_id": "main_cam",
            }
        )
        for node in shot_nodes[name]:
            if node in ("main_cam", "paper_bg"):
                continue
            scene["animation_tracks"].append(
                opacity_track(f"{name}_{node}_fade", node, start, end)
            )

    blends = [
        ("t_ps_co", "problem_statement", "construction", 250, 8, "wipe"),
        ("t_co_de", "construction", "derivation", 634, 10, "dissolve"),
        ("t_de_vi", "derivation", "visual_intuition", 1154, 6, "cut"),
        ("t_vi_su", "visual_intuition", "summary", 1543, 1, "cut"),
    ]
    for tid, frm, to, start, dur, blend in blends:
        scene["transition_spans"].append(
            {
                "id": tid,
                "from_shot_id": frm,
                "to_shot_id": to,
                "start_frame": start,
                "duration_frames": dur,
                "blend": blend,
            }
        )

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    digest = hashlib.sha256(OUT.read_bytes()).hexdigest()
    print(OUT.relative_to(ROOT).as_posix())
    print(digest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
