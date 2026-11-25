"""Allowlisted technical probe for silent vertical reference MP4s.

Uses ffprobe when available. Collects only explicit technical fields — never
paths outside the provided file name, hostnames, or device identity.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path
from typing import Any


ALLOWED_CODEC_NAMES = {"h264", "avc1", "avc"}


def sha256_file(path: Path) -> str:
    import hashlib

    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _ffprobe(path: Path) -> dict[str, Any]:
    ffprobe = shutil.which("ffprobe")
    if ffprobe is None:
        raise RuntimeError("ffprobe is required for MP4 technical probe")
    completed = subprocess.run(
        [
            ffprobe,
            "-v",
            "error",
            "-print_format",
            "json",
            "-show_format",
            "-show_streams",
            str(path),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"ffprobe failed ({completed.returncode}): {completed.stderr.strip()}"
        )
    return json.loads(completed.stdout)


def probe_mp4(path: Path) -> dict[str, Any]:
    """Return allowlisted technical fields for a local MP4 file."""
    if not path.is_file():
        raise RuntimeError(f"missing mp4: {path.name}")

    raw = _ffprobe(path)
    streams = raw.get("streams") or []
    video_streams = [s for s in streams if s.get("codec_type") == "video"]
    audio_streams = [s for s in streams if s.get("codec_type") == "audio"]
    if len(video_streams) != 1:
        raise RuntimeError(
            f"{path.name}: expected exactly one video stream, got {len(video_streams)}"
        )

    video = video_streams[0]
    width = int(video.get("width") or 0)
    height = int(video.get("height") or 0)
    codec = str(video.get("codec_name") or "").lower()

    fps_text = str(video.get("avg_frame_rate") or video.get("r_frame_rate") or "0/1")
    if "/" in fps_text:
        num_s, den_s = fps_text.split("/", 1)
        numerator = float(num_s)
        denominator = float(den_s) if float(den_s) != 0 else 1.0
        nominal_fps = numerator / denominator
    else:
        nominal_fps = float(fps_text or 0)

    duration_sec = None
    format_block = raw.get("format") or {}
    if format_block.get("duration") is not None:
        duration_sec = float(format_block["duration"])
    elif video.get("duration") is not None:
        duration_sec = float(video["duration"])

    return {
        "schema_version": 1,
        "filename": path.name,
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
        "width": width,
        "height": height,
        "nominal_fps": round(nominal_fps, 3),
        "duration_sec": None if duration_sec is None else round(duration_sec, 3),
        "video_track_count": len(video_streams),
        "audio_track_count": len(audio_streams),
        "codec_name": codec,
        "certified_performance": False,
    }


def validate_probe(
    probe: dict[str, Any],
    requirements: dict[str, Any],
    *,
    expected_sha256: str | None = None,
) -> list[str]:
    errors: list[str] = []
    name = probe.get("filename", "<mp4>")
    if probe.get("width") != requirements.get("width"):
        errors.append(f"{name}: width {probe.get('width')} != {requirements.get('width')}")
    if probe.get("height") != requirements.get("height"):
        errors.append(
            f"{name}: height {probe.get('height')} != {requirements.get('height')}"
        )
    if probe.get("video_track_count") != requirements.get("video_track_count", 1):
        errors.append(f"{name}: video_track_count mismatch")
    if probe.get("audio_track_count") != requirements.get("audio_track_count", 0):
        errors.append(f"{name}: audio tracks present (must be silent)")

    fps = float(probe.get("nominal_fps") or 0)
    expected_fps = float(requirements.get("nominal_fps") or 30)
    if abs(fps - expected_fps) > 0.05:
        errors.append(f"{name}: nominal_fps {fps} != {expected_fps}")

    duration = probe.get("duration_sec")
    expected_duration = float(requirements.get("duration_sec") or 60)
    if duration is None:
        errors.append(f"{name}: missing duration")
    elif abs(float(duration) - expected_duration) > 0.15:
        errors.append(
            f"{name}: duration_sec {duration} not within 0.15s of {expected_duration}"
        )

    allow = set(requirements.get("codec_name_allowlist") or list(ALLOWED_CODEC_NAMES))
    codec = str(probe.get("codec_name") or "").lower()
    if codec not in allow:
        errors.append(f"{name}: codec {codec!r} not in {sorted(allow)}")

    if expected_sha256 is not None and probe.get("sha256") != expected_sha256:
        errors.append(f"{name}: sha256 mismatch vs catalog")
    return errors
