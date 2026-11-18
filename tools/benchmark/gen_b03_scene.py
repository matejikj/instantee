#!/usr/bin/env python3
"""Generate the locked B-03 historical Paper Cinema 60 s Scene IR."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "contracts/fixtures/b03-paper/b03-historical-reel.scene.json"

FONT = (
    "assets/paper-cinema/fonts/NotoSans-Regular.ttf",
    "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5",
)
FIBER = (
    "assets/paper-cinema/textures/paper-fiber.png",
    "383f7997841ab6af5a3c864400c571814dbc0d90e21a9623b0d4c7c9d8ef1985",
)
WASH = (
    "assets/paper-cinema/textures/paper-wash.png",
    "3abce6e41885ac53192011c339ceb700d0fe46a24ee4944383442dbf623f085b",
)
SILHOUETTE = (
    "assets/paper-cinema/patterns/silhouette-card.png",
    "d955366e87ae45aba50f899cbcb5dcef917f098b7b0737bc436035637f4eaef0",
)


def rect(w: float, h: float) -> list[list[float]]:
    return [[0, 0], [w, 0], [w, h], [0, h]]


def opacity_track(
    track_id: str,
    target: str,
    start: int,
    end: int,
    fade: int = 12,
    *,
    fade_out_frame: int | None = None,
):
    mid = min(start + fade, end)
    keyframes = [
        {"frame": start, "value": 0.0},
        {"frame": mid, "value": 1.0},
        {"frame": end, "value": 1.0},
    ]
    if fade_out_frame is not None:
        if fade_out_frame <= end:
            raise ValueError("fade_out_frame must be after the shot end")
        keyframes.append({"frame": fade_out_frame, "value": 0.0})
    return {
        "id": track_id,
        "target_id": target,
        "property": "opacity",
        "interpolation": {"type": "linear"},
        "keyframes": keyframes,
    }


def translate_track(
    track_id: str, target: str, prop: str, keys: list[tuple[int, float]]
):
    return {
        "id": track_id,
        "target_id": target,
        "property": prop,
        "interpolation": {"type": "linear"},
        "keyframes": [{"frame": f, "value": v} for f, v in keys],
    }


def text_run(run_id: str, text: str, size: int, width: int, role: str):
    return {
        "id": run_id,
        "text": text,
        "font_asset_id": "font_display",
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


def geom_node(
    node_id: str, geom: str, material: str, x: float, y: float, z: float, layer: int
):
    return {
        "id": node_id,
        "kind": "geometry",
        "layer_order": layer,
        "geometry_id": geom,
        "material_id": material,
        "visible": True,
        "transform": {
            "translate": {"x": x, "y": y, "z": z},
            "opacity": 0,
            "scale": {"x": 1, "y": 1, "z": 1},
            "rotation_z_rad": 0,
        },
    }


def image_node(
    node_id: str, asset: str, material: str, x: float, y: float, z: float, layer: int
):
    return {
        "id": node_id,
        "kind": "image",
        "layer_order": layer,
        "material_id": material,
        "visible": True,
        "transform": {
            "translate": {"x": x, "y": y, "z": z},
            "opacity": 0,
        },
        "asset_id": asset,
    }


def main() -> int:
    # Five Paper Cinema shot templates packed to 1800 frames @ 30 fps.
    shots = [
        ("reveal_stage", 0, 360),
        ("object_closeup", 360, 300),
        ("puppet_dialogue", 660, 420),
        ("technical_explanation", 1080, 360),
        ("atmospheric_dissolve", 1440, 360),
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
                "uri": FONT[0],
                "sha256": FONT[1],
                "media_type": "font/ttf",
                "color_profile": "untagged",
            },
            {
                "id": "paper_fiber",
                "uri": FIBER[0],
                "sha256": FIBER[1],
                "media_type": "image/png",
                "color_profile": "untagged",
            },
            {
                "id": "paper_wash",
                "uri": WASH[0],
                "sha256": WASH[1],
                "media_type": "image/png",
                "color_profile": "untagged",
            },
            {
                "id": "silhouette",
                "uri": SILHOUETTE[0],
                "sha256": SILHOUETTE[1],
                "media_type": "image/png",
                "color_profile": "untagged",
            },
        ],
        "materials": [
            {
                "id": "ink",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.14, "g": 0.10, "b": 0.08, "a": 1},
            },
            {
                "id": "pulp",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.90, "g": 0.84, "b": 0.72, "a": 1},
            },
            {
                "id": "accent",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.55, "g": 0.22, "b": 0.16, "a": 1},
            },
            {
                "id": "sky",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.72, "g": 0.78, "b": 0.86, "a": 1},
            },
            {
                "id": "veil",
                "shader_feature": "solid-fill",
                "opacity": 1,
                "parameters": {"r": 0.14, "g": 0.17, "b": 0.23, "a": 1},
            },
        ],
        "geometry": [
            {"id": "stage_floor", "kind": "path", "closed": True, "points": rect(980, 220)},
            {"id": "fold_card", "kind": "path", "closed": True, "points": rect(420, 560)},
            {"id": "torso", "kind": "path", "closed": True, "points": rect(180, 260)},
            {"id": "head", "kind": "path", "closed": True, "points": rect(120, 140)},
            {"id": "arm", "kind": "path", "closed": True, "points": rect(70, 220)},
            {"id": "prop_object", "kind": "path", "closed": True, "points": rect(200, 200)},
            {"id": "diagram_bar", "kind": "path", "closed": True, "points": rect(640, 28)},
        ],
        "rigs": [
            {
                "id": "historical-paper-figure",
                "format_version": "1.0.0",
                "payload_ref": "contracts/fixtures/puppet-reference-pack/manifest.txt",
            }
        ],
        "text_runs": [
            text_run("txt_rs_title", "Victorian workshop", 56, 980, "headline"),
            text_run("txt_oc_title", "Object study", 52, 980, "headline"),
            text_run("txt_pd_title", "Dialogue of paper figures", 48, 980, "headline"),
            text_run("txt_te_title", "Fold mechanism", 52, 980, "headline"),
            text_run(
                "txt_ad_title",
                "Atmosphere dissolves to dusk",
                48,
                980,
                "headline",
            ),
        ],
        "cameras": [
            {
                "id": "main_cam",
                "projection": "perspective",
                "fov_y_rad": 0.6981317007977318,
                "near": 0.1,
                "far": 2000,
            }
        ],
        "nodes": [
            {
                "id": "camera",
                "kind": "camera",
                "layer_order": 1000,
                "camera_id": "main_cam",
                "visible": True,
                "transform": {
                    "translate": {"x": 0, "y": 0, "z": 0},
                    "opacity": 1,
                },
            },
            image_node("bg_wash", "paper_wash", "sky", 0, 0, -40, -20),
            image_node("bg_far", "silhouette", "sky", 40, 420, -30, -10),
            image_node("bg_fiber", "paper_fiber", "pulp", 50, 1500, -5, 5),
            geom_node("stage", "stage_floor", "pulp", 50, 1500, -2, 10),
            geom_node("fold", "fold_card", "pulp", 330, 620, -1, 20),
            {
                "id": "figure",
                "kind": "puppet",
                "layer_order": 40,
                "rig_id": "historical-paper-figure",
                "material_id": "accent",
                "visible": True,
                "transform": {
                    "translate": {"x": 420, "y": 780, "z": 0},
                    "opacity": 0,
                    "scale": {"x": 1, "y": 1, "z": 1},
                },
            },
            geom_node("figure_torso", "torso", "accent", 450, 980, 1, 41),
            geom_node("figure_head", "head", "ink", 480, 820, 2, 42),
            geom_node("figure_arm", "arm", "accent", 620, 980, 2, 43),
            geom_node("prop", "prop_object", "ink", 440, 900, 3, 50),
            geom_node("diagram", "diagram_bar", "ink", 220, 1100, 1, 55),
            image_node("veil_overlay", "paper_wash", "veil", 0, 0, 8, 80),
            text_node("rs_title", "txt_rs_title", "ink", 50, 120, 60),
            text_node("oc_title", "txt_oc_title", "ink", 50, 120, 61),
            text_node("pd_title", "txt_pd_title", "ink", 50, 120, 62),
            text_node("te_title", "txt_te_title", "ink", 50, 120, 63),
            text_node("ad_title", "txt_ad_title", "ink", 50, 120, 64),
        ],
        "shots": [],
        "transition_spans": [],
        "animation_tracks": [],
        "provenance": {
            "project_sha256": "60a575496e2f962ab36aa12b7f437714cf9a10732245417e83a174370ec8b4bc",
            "style_packages": [{"id": "paper-cinema", "version": "1.0.0"}],
        },
    }

    tracks: list[dict] = []

    # Persistent stage + backgrounds across the reel (fade in once).
    for nid in ("bg_wash", "bg_far", "bg_fiber", "stage"):
        tracks.append(
            {
                "id": f"{nid}_opacity",
                "target_id": nid,
                "property": "opacity",
                "interpolation": {"type": "linear"},
                "keyframes": [
                    {"frame": 0, "value": 0.0},
                    {"frame": 18, "value": 1.0},
                    {"frame": 1799, "value": 1.0},
                ],
            }
        )

    # Shot-local titles (one opacity track per node).
    title_shots = {
        "rs_title": "reveal_stage",
        "oc_title": "object_closeup",
        "pd_title": "puppet_dialogue",
        "te_title": "technical_explanation",
        "ad_title": "atmospheric_dissolve",
    }
    shot_span = {
        sid: (start, start + dur - 1, min(start + dur, 1799))
        for sid, start, dur in shots
    }
    for nid, sid in title_shots.items():
        start, end, fade_out = shot_span[sid]
        tracks.append(
            opacity_track(
                f"{nid}_opacity",
                nid,
                start,
                end,
                fade=10,
                fade_out_frame=fade_out if fade_out > end else None,
            )
        )

    # Figure layers appear in reveal + dialogue + atmosphere (single track each).
    def multi_window_opacity(track_id: str, target: str, windows: list[tuple[int, int]]):
        keys: list[dict] = [{"frame": 0, "value": 0.0}]
        for start, end in windows:
            keys.append({"frame": start, "value": 0.0})
            keys.append({"frame": min(start + 10, end), "value": 1.0})
            keys.append({"frame": end, "value": 1.0})
            fade = min(end + 1, 1799)
            if fade > end:
                keys.append({"frame": fade, "value": 0.0})
        keys.append({"frame": 1799, "value": keys[-1]["value"]})
        # Deduplicate frames keeping last value.
        ordered: dict[int, float] = {}
        for key in keys:
            ordered[int(key["frame"])] = float(key["value"])
        return {
            "id": track_id,
            "target_id": target,
            "property": "opacity",
            "interpolation": {"type": "linear"},
            "keyframes": [
                {"frame": frame, "value": value}
                for frame, value in sorted(ordered.items())
            ],
        }

    figure_windows = [
        (0, 359),
        (660, 1079),
        (1440, 1799),
    ]
    for nid in ("figure", "figure_torso", "figure_head"):
        tracks.append(multi_window_opacity(f"{nid}_opacity", nid, figure_windows))
    tracks.append(multi_window_opacity("figure_arm_opacity", "figure_arm", [(0, 359), (660, 1079)]))
    tracks.append(multi_window_opacity("prop_opacity", "prop", [(360, 659)]))
    tracks.append(multi_window_opacity("fold_opacity", "fold", [(1080, 1439)]))
    tracks.append(multi_window_opacity("diagram_opacity", "diagram", [(1080, 1439)]))

    # Gesture-like arm raise during dialogue (rotation).
    tracks.append(
        {
            "id": "arm_raise",
            "target_id": "figure_arm",
            "property": "rotation_z_rad",
            "interpolation": {"type": "linear"},
            "keyframes": [
                {"frame": 660, "value": 0.0},
                {"frame": 780, "value": -0.85},
                {"frame": 960, "value": -0.85},
                {"frame": 1040, "value": 0.0},
            ],
        }
    )

    # Fold hinge during technical explanation.
    tracks.append(
        {
            "id": "fold_hinge",
            "target_id": "fold",
            "property": "rotation_z_rad",
            "interpolation": {"type": "linear"},
            "keyframes": [
                {"frame": 1080, "value": 0.0},
                {"frame": 1200, "value": -1.2},
                {"frame": 1320, "value": -1.2},
                {"frame": 1420, "value": 0.0},
            ],
        }
    )

    # Cinematic camera presets as TRS tracks (one track per property).
    tracks.append(
        translate_track(
            "cam_translate_z",
            "camera",
            "translate_z",
            [
                (0, 0.0),
                (180, -40.0),
                (360, -40.0),
                (1080, -40.0),
                (1260, -28.0),
                (1440, -28.0),
                (1799, -28.0),
            ],
        )
    )
    tracks.append(
        translate_track(
            "cam_scale_x",
            "camera",
            "scale_x",
            [(0, 1.0), (360, 1.0), (480, 1.18), (660, 1.18), (1799, 1.18)],
        )
    )
    tracks.append(
        translate_track(
            "cam_scale_y",
            "camera",
            "scale_y",
            [(0, 1.0), (360, 1.0), (480, 1.18), (660, 1.18), (1799, 1.18)],
        )
    )
    tracks.append(
        translate_track(
            "cam_translate_x",
            "camera",
            "translate_x",
            [(0, 0.0), (660, 0.0), (820, 36.0), (1080, 36.0), (1799, 36.0)],
        )
    )
    tracks.append(
        translate_track(
            "cam_translate_y",
            "camera",
            "translate_y",
            [(0, 0.0), (1440, 0.0), (1620, -48.0), (1799, -48.0)],
        )
    )

    # Atmospheric veil rises through the final shot.
    tracks.append(
        {
            "id": "veil_rise",
            "target_id": "veil_overlay",
            "property": "opacity",
            "interpolation": {"type": "linear"},
            "keyframes": [
                {"frame": 1440, "value": 0.0},
                {"frame": 1560, "value": 0.35},
                {"frame": 1799, "value": 0.55},
            ],
        }
    )

    scene["animation_tracks"] = tracks

    # Shot membership: shared stage + shot-local content (loader requires node_ids).
    shared = ["camera", "bg_wash", "bg_far", "bg_fiber", "stage"]
    shot_nodes = {
        "reveal_stage": shared
        + ["rs_title", "figure", "figure_torso", "figure_head", "figure_arm"],
        "object_closeup": shared + ["oc_title", "prop"],
        "puppet_dialogue": shared
        + ["pd_title", "figure", "figure_torso", "figure_head", "figure_arm"],
        "technical_explanation": shared + ["te_title", "fold", "diagram"],
        "atmospheric_dissolve": shared
        + [
            "ad_title",
            "figure",
            "figure_torso",
            "figure_head",
            "veil_overlay",
        ],
    }
    scene["shots"] = [
        {
            "id": sid,
            "start_frame": start,
            "duration_frames": dur,
            "node_ids": shot_nodes[sid],
            "camera_node_id": "camera",
        }
        for sid, start, dur in shots
    ]
    # Dissolve into the final atmospheric shot (Scene IR blend).
    scene["transition_spans"] = [
        {
            "id": "tech_to_atmosphere",
            "from_shot_id": "technical_explanation",
            "to_shot_id": "atmospheric_dissolve",
            "start_frame": 1434,
            "duration_frames": 12,
            "blend": "dissolve",
        }
    ]

    OUT.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(scene, indent=2) + "\n"
    OUT.write_text(text, encoding="utf-8")
    digest = hashlib.sha256(OUT.read_bytes()).hexdigest()
    print(OUT.relative_to(ROOT))
    print(digest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
