"""Workload lock helpers: pin digests and detect baseline-breaking drift.

A workload_id is the baseline identity. Any change to a hashed input without a
new workload_id fails verification — regression must compare the same lock.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = (
    REPO_ROOT / "contracts" / "fixtures" / "benchmark-workloads" / "catalog-v1.json"
)

REQUIRED_LOCK_FIELDS = (
    "workload_id",
    "scene",
    "scene_sha256",
    "asset_lock",
    "asset_lock_sha256",
    "style_bible",
    "style_sha256",
    "quality_tier",
    "encoder_profile",
    "duration_frames",
    "width",
    "height",
    "frame_rate",
    "seed_decimal",
)

OPTIONAL_FILE_HASH_FIELDS = (
    ("style_bible", "style_sha256"),
)

OPTIONAL_BLOB_HASH_FIELDS = (
    "font_primary_sha256",
    "font_math_sha256",
)


class WorkloadLockError(ValueError):
    """Raised when a workload lock fails structural or digest checks."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise WorkloadLockError(f"{path}: root must be an object")
    return payload


def _hex64(value: Any, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64:
        raise WorkloadLockError(f"{label} must be 64 lowercase hex digits")
    if any(ch not in "0123456789abcdef" for ch in value):
        raise WorkloadLockError(f"{label} must be 64 lowercase hex digits")
    return value


def verify_workload_lock(
    lock_path: Path,
    *,
    root: Path = REPO_ROOT,
) -> dict[str, Any]:
    """Return a verification record; raises WorkloadLockError on drift."""
    lock = load_json(lock_path)
    for field in REQUIRED_LOCK_FIELDS:
        if field not in lock:
            raise WorkloadLockError(f"{lock_path.name}: missing {field}")

    workload_id = lock["workload_id"]
    if not isinstance(workload_id, str) or not workload_id.strip():
        raise WorkloadLockError(f"{lock_path.name}: workload_id must be non-empty")

    scene_rel = lock["scene"]
    asset_rel = lock["asset_lock"]
    style_rel = lock["style_bible"]
    for rel, label in (
        (scene_rel, "scene"),
        (asset_rel, "asset_lock"),
        (style_rel, "style_bible"),
    ):
        if not isinstance(rel, str) or Path(rel).is_absolute() or ".." in Path(rel).parts:
            raise WorkloadLockError(f"{lock_path.name}: {label} must be repo-relative")

    scene_path = root / scene_rel
    asset_path = root / asset_rel
    style_path = root / style_rel
    for path, label in (
        (scene_path, "scene"),
        (asset_path, "asset_lock"),
        (style_path, "style_bible"),
    ):
        if not path.is_file():
            raise WorkloadLockError(f"{lock_path.name}: missing {label} file {path}")

    expected_scene = _hex64(lock["scene_sha256"], "scene_sha256")
    expected_asset = _hex64(lock["asset_lock_sha256"], "asset_lock_sha256")
    expected_style = _hex64(lock["style_sha256"], "style_sha256")

    actual_scene = sha256_file(scene_path)
    actual_asset = sha256_file(asset_path)
    actual_style = sha256_file(style_path)

    mismatches: list[str] = []
    if actual_scene != expected_scene:
        mismatches.append(
            f"scene digest drifted (locked {expected_scene[:12]}… actual {actual_scene[:12]}…); "
            f"bump workload_id for a new baseline"
        )
    if actual_asset != expected_asset:
        mismatches.append(
            f"asset_lock digest drifted; bump workload_id for a new baseline"
        )
    if actual_style != expected_style:
        mismatches.append(
            f"style_bible digest drifted; bump workload_id for a new baseline"
        )

    duration = lock["duration_frames"]
    if type(duration) is not int or duration < 1:
        raise WorkloadLockError(f"{lock_path.name}: duration_frames must be positive int")

    frame_rate = lock["frame_rate"]
    if not isinstance(frame_rate, dict):
        raise WorkloadLockError(f"{lock_path.name}: frame_rate must be an object")
    if type(frame_rate.get("numerator")) is not int or frame_rate["numerator"] < 1:
        raise WorkloadLockError(f"{lock_path.name}: invalid frame_rate.numerator")
    if type(frame_rate.get("denominator")) is not int or frame_rate["denominator"] < 1:
        raise WorkloadLockError(f"{lock_path.name}: invalid frame_rate.denominator")

    # Optional font blob pins: presence is advisory unless a search path is given.
    font_pins = {
        key: _hex64(lock[key], key)
        for key in OPTIONAL_BLOB_HASH_FIELDS
        if key in lock and lock[key] is not None
    }

    if mismatches:
        raise WorkloadLockError(
            f"{workload_id}: " + "; ".join(mismatches)
        )

    record = {
        "workload_id": workload_id,
        "lock_path": str(lock_path.relative_to(root)),
        "scene": scene_rel,
        "scene_sha256": actual_scene,
        "asset_lock": asset_rel,
        "asset_lock_sha256": actual_asset,
        "style_bible": style_rel,
        "style_sha256": actual_style,
        "quality_tier": lock["quality_tier"],
        "encoder_profile": lock["encoder_profile"],
        "duration_frames": duration,
        "width": lock["width"],
        "height": lock["height"],
        "frame_rate": frame_rate,
        "seed_decimal": lock["seed_decimal"],
        "font_pins": font_pins,
        "ok": True,
    }
    record["entry_digest"] = sha256_bytes(
        json.dumps(
            {
                "workload_id": workload_id,
                "scene_sha256": actual_scene,
                "asset_lock_sha256": actual_asset,
                "style_sha256": actual_style,
                "quality_tier": lock["quality_tier"],
                "encoder_profile": lock["encoder_profile"],
                "duration_frames": duration,
                "width": lock["width"],
                "height": lock["height"],
                "frame_rate": frame_rate,
                "seed_decimal": lock["seed_decimal"],
                "font_pins": font_pins,
            },
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    )
    return record


def load_catalog(path: Path = CATALOG_PATH) -> dict[str, Any]:
    catalog = load_json(path)
    if catalog.get("schema_version") != "benchmark-workload-catalog-v1":
        raise WorkloadLockError("unsupported benchmark workload catalog schema")
    entries = catalog.get("workloads")
    if not isinstance(entries, list) or not entries:
        raise WorkloadLockError("catalog workloads must be a non-empty array")
    return catalog


def verify_catalog(
    *,
    root: Path = REPO_ROOT,
    catalog_path: Path = CATALOG_PATH,
) -> dict[str, Any]:
    catalog = load_catalog(catalog_path)
    reports: list[dict[str, Any]] = []
    for entry in catalog["workloads"]:
        if not isinstance(entry, dict):
            raise WorkloadLockError("catalog entry must be an object")
        for field in ("id", "lock", "status", "family"):
            if field not in entry:
                raise WorkloadLockError(f"catalog entry missing {field}")
        lock_path = root / entry["lock"]
        if not lock_path.is_file():
            raise WorkloadLockError(f"catalog lock missing: {entry['lock']}")
        report = verify_workload_lock(lock_path, root=root)
        if report["workload_id"] != entry["id"]:
            raise WorkloadLockError(
                f"catalog id {entry['id']} != lock workload_id {report['workload_id']}"
            )
        if entry.get("duration_frames") is not None and entry["duration_frames"] != report[
            "duration_frames"
        ]:
            raise WorkloadLockError(
                f"{entry['id']}: catalog duration_frames disagrees with lock"
            )
        if entry.get("quality_tier") is not None and entry["quality_tier"] != report[
            "quality_tier"
        ]:
            raise WorkloadLockError(
                f"{entry['id']}: catalog quality_tier disagrees with lock"
            )
        reports.append(
            {
                **report,
                "family": entry["family"],
                "status": entry["status"],
                "measurement_gate": entry.get("measurement_gate"),
            }
        )

    catalog_digest = sha256_bytes(
        json.dumps(
            [{"workload_id": r["workload_id"], "entry_digest": r["entry_digest"]} for r in reports],
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    )
    expected = catalog.get("catalog_sha256")
    if expected is not None:
        expected = _hex64(expected, "catalog_sha256")
        if expected != catalog_digest:
            raise WorkloadLockError(
                "catalog_sha256 drifted from verified workload digests; "
                "update the catalog digest after an intentional baseline change"
            )

    return {
        "ok": True,
        "catalog": str(catalog_path.relative_to(root)),
        "catalog_sha256": catalog_digest,
        "workloads": reports,
    }
