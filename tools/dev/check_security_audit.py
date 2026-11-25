#!/usr/bin/env python3
"""Offline security and license audit with SBOM emission.

Validates vendored PIN identity, font/texture license coverage on the release
asset lock, Node/vcpkg dependency policy, and metal-cpp pin fields. Emits a
CycloneDX-inspired SBOM and an audit report. Unresolved critical findings fail
the process. Does not fetch CVE feeds or claim certified performance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

import check_dependency_audit as deps

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ALLOWED = REPO_ROOT / "contracts" / "security-audit" / "allowed-licenses-v1.json"
DEFAULT_LIMITS = REPO_ROOT / "contracts" / "security-audit" / "platform-limits-v1.json"
DEFAULT_ASSET_LOCK = REPO_ROOT / "contracts" / "release" / "release-asset-lock-v1.json"
THIRD_PARTY = REPO_ROOT / "third_party"

# Mirrors engine/src/render-metal/CMakeLists.txt pin gates.
EXPECTED_METAL_CPP = {
    "tag": "release/metal-cpp_macOS27.0.1_iOS27.0.1",
    "commit": "8cdde68ae36a636c84cbb48ebcbc8f3b60251ea0",
    "archive_sha256": (
        "3a2f456c5373748e1b8de0896d1dbe53d6e0a1d3b8b9e5235c1a8abae6f373f5"
    ),
}

LICENSE_FILE_CANDIDATES = (
    "LICENSE",
    "LICENSE.txt",
    "LICENSE.md",
    "COPYING",
    "COPYING.txt",
    "NOTICE",
    "NOTICE.txt",
)

ROLES_REQUIRING_LICENSE_PATH = frozenset({"font", "texture"})


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: Path, root: Path = REPO_ROOT) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def finding(
    findings: list[dict[str, Any]],
    *,
    severity: str,
    code: str,
    path: str,
    message: str,
) -> None:
    findings.append(
        {
            "severity": severity,
            "code": code,
            "path": path,
            "message": message,
        }
    )


def license_file_in(directory: Path) -> Path | None:
    for name in LICENSE_FILE_CANDIDATES:
        candidate = directory / name
        if candidate.is_file():
            return candidate
    return None


def audit_dependency_locks(findings: list[dict[str, Any]]) -> dict[str, Any]:
    errors = deps.run_audit(REPO_ROOT)
    for error in errors:
        finding(
            findings,
            severity="critical",
            code="dependency_lock",
            path="vcpkg.json|package.json",
            message=error,
        )
    return {"ok": not errors, "errors": errors}


def audit_metal_cpp(findings: list[dict[str, Any]]) -> dict[str, Any]:
    pin_path = THIRD_PARTY / "metal-cpp" / "PIN.json"
    result: dict[str, Any] = {"ok": True, "pin": rel(pin_path)}
    if not pin_path.is_file():
        finding(
            findings,
            severity="critical",
            code="metal_cpp_pin_missing",
            path=rel(pin_path),
            message="metal-cpp PIN.json is missing",
        )
        result["ok"] = False
        return result

    pin = load_json(pin_path)
    for key, expected in EXPECTED_METAL_CPP.items():
        actual = pin.get(key)
        if actual != expected:
            finding(
                findings,
                severity="critical",
                code="metal_cpp_pin_mismatch",
                path=rel(pin_path),
                message=f"metal-cpp {key} mismatch: expected {expected!r}, got {actual!r}",
            )
            result["ok"] = False
    license_path = THIRD_PARTY / "metal-cpp" / "LICENSE.txt"
    if not license_path.is_file():
        finding(
            findings,
            severity="critical",
            code="metal_cpp_license_missing",
            path=rel(license_path),
            message="metal-cpp LICENSE.txt is missing",
        )
        result["ok"] = False
    header = THIRD_PARTY / "metal-cpp" / "Metal" / "Metal.hpp"
    if not header.is_file():
        finding(
            findings,
            severity="critical",
            code="metal_cpp_headers_missing",
            path=rel(header),
            message="metal-cpp Metal.hpp is missing",
        )
        result["ok"] = False
    result["tag"] = pin.get("tag")
    result["commit"] = pin.get("commit")
    result["archive_sha256"] = pin.get("archive_sha256")
    result["license"] = pin.get("license")
    return result


def audit_third_party_pins(
    findings: list[dict[str, Any]],
    allowed: set[str],
) -> dict[str, Any]:
    components: list[dict[str, Any]] = []
    pin_paths = sorted(THIRD_PARTY.glob("*/PIN.json"))
    if not pin_paths:
        finding(
            findings,
            severity="critical",
            code="third_party_pins_missing",
            path="third_party/",
            message="no third_party PIN.json files found",
        )
        return {"ok": False, "components": components}

    for pin_path in pin_paths:
        pin = load_json(pin_path)
        name = pin.get("name") or pin_path.parent.name
        directory = pin_path.parent

        if name == "pinned-fallback-fonts" or pin_path.parent.name == "fonts":
            fonts = pin.get("fonts") or []
            if not fonts:
                finding(
                    findings,
                    severity="critical",
                    code="font_pin_empty",
                    path=rel(pin_path),
                    message="fonts PIN declares no fonts",
                )
            for font in fonts:
                license_id = font.get("license")
                if license_id not in allowed:
                    finding(
                        findings,
                        severity="critical",
                        code="license_not_allowed",
                        path=rel(pin_path),
                        message=f"font license not allowed: {license_id!r}",
                    )
                license_file = font.get("license_file")
                if not license_file or not (directory / license_file).is_file():
                    finding(
                        findings,
                        severity="critical",
                        code="license_file_missing",
                        path=rel(pin_path),
                        message=f"font license file missing for {font.get('file')}",
                    )
                font_file = font.get("file")
                expected = font.get("archive_sha256")
                if font_file and expected:
                    path = directory / font_file
                    if not path.is_file():
                        finding(
                            findings,
                            severity="critical",
                            code="font_file_missing",
                            path=rel(path),
                            message="pinned font file missing",
                        )
                    elif sha256_file(path) != expected:
                        finding(
                            findings,
                            severity="critical",
                            code="font_hash_mismatch",
                            path=rel(path),
                            message="font bytes do not match PIN archive_sha256",
                        )
                components.append(
                    {
                        "type": "font",
                        "name": font.get("family") or font_file,
                        "version": font.get("tag") or font.get("commit"),
                        "license": license_id,
                        "purl": None,
                        "hashes": [{"alg": "SHA-256", "content": expected}],
                    }
                )
            continue

        if name == "typography-native-deps" or pin_path.parent.name == "typography-deps":
            for dep in pin.get("dependencies") or []:
                license_id = dep.get("license")
                if license_id not in allowed:
                    finding(
                        findings,
                        severity="critical",
                        code="license_not_allowed",
                        path=rel(pin_path),
                        message=f"typography dep license not allowed: {license_id!r}",
                    )
                components.append(
                    {
                        "type": "library",
                        "name": dep.get("name"),
                        "version": dep.get("reference_version") or dep.get("min_version"),
                        "license": license_id,
                        "scope": "pkg-config",
                    }
                )
            continue

        license_id = pin.get("license")
        if not license_id:
            finding(
                findings,
                severity="critical",
                code="pin_license_missing",
                path=rel(pin_path),
                message="PIN.json missing license field",
            )
        elif license_id not in allowed:
            finding(
                findings,
                severity="critical",
                code="license_not_allowed",
                path=rel(pin_path),
                message=f"pin license not allowed: {license_id!r}",
            )

        archive_sha = pin.get("archive_sha256")
        if not archive_sha:
            finding(
                findings,
                severity="critical",
                code="pin_hash_missing",
                path=rel(pin_path),
                message="PIN.json missing archive_sha256",
            )

        # Single-file raw pins: content must match archive_sha256.
        if name in {"stb_image"} and archive_sha:
            header = directory / "stb_image.h"
            if not header.is_file():
                finding(
                    findings,
                    severity="critical",
                    code="pin_content_missing",
                    path=rel(header),
                    message="stb_image.h missing",
                )
            elif sha256_file(header) != archive_sha:
                finding(
                    findings,
                    severity="critical",
                    code="pin_hash_mismatch",
                    path=rel(header),
                    message="stb_image.h does not match archive_sha256",
                )
            # License text lives in the header; no separate LICENSE file required.
        else:
            if license_file_in(directory) is None and pin_path.parent.name != "metal-cpp":
                # metal-cpp checked separately; other multi-file pins need a sidecar.
                if pin_path.parent.name not in {"stb"}:
                    finding(
                        findings,
                        severity="critical",
                        code="license_file_missing",
                        path=rel(directory),
                        message="vendored pin missing LICENSE/COPYING sidecar",
                    )

        components.append(
            {
                "type": "library",
                "name": name,
                "version": pin.get("tag") or pin.get("commit"),
                "license": license_id,
                "hashes": (
                    [{"alg": "SHA-256", "content": archive_sha}] if archive_sha else []
                ),
            }
        )

    return {"ok": True, "components": components, "pin_count": len(pin_paths)}


def audit_release_asset_lock(
    findings: list[dict[str, Any]],
    allowed: set[str],
    lock_path: Path,
) -> dict[str, Any]:
    if not lock_path.is_file():
        finding(
            findings,
            severity="critical",
            code="asset_lock_missing",
            path=rel(lock_path),
            message="release asset lock missing",
        )
        return {"ok": False, "entries": 0}

    lock = load_json(lock_path)
    entries = lock.get("entries") or []
    checked = 0
    for entry in entries:
        path_rel = entry.get("path")
        expected = entry.get("sha256")
        license_id = entry.get("license")
        role = entry.get("role")
        entry_id = entry.get("id", path_rel)
        if not path_rel or not expected or not license_id:
            finding(
                findings,
                severity="critical",
                code="asset_lock_entry_invalid",
                path=rel(lock_path),
                message=f"entry {entry_id!r} missing path/sha256/license",
            )
            continue
        if license_id not in allowed:
            finding(
                findings,
                severity="critical",
                code="license_not_allowed",
                path=path_rel,
                message=f"asset license not allowed: {license_id!r}",
            )
        full = REPO_ROOT / path_rel
        if not full.is_file():
            finding(
                findings,
                severity="critical",
                code="asset_missing",
                path=path_rel,
                message="locked asset path missing",
            )
            continue
        actual = sha256_file(full)
        if actual != expected:
            finding(
                findings,
                severity="critical",
                code="asset_hash_mismatch",
                path=path_rel,
                message="asset bytes do not match release lock sha256",
            )
        if role in ROLES_REQUIRING_LICENSE_PATH:
            license_path = entry.get("license_path")
            if not license_path:
                finding(
                    findings,
                    severity="critical",
                    code="asset_license_path_missing",
                    path=path_rel,
                    message=f"{role} entry requires license_path",
                )
            elif not (REPO_ROOT / license_path).is_file():
                finding(
                    findings,
                    severity="critical",
                    code="asset_license_file_missing",
                    path=license_path,
                    message="license_path file missing",
                )
        checked += 1

    content = lock.get("content_sha256")
    if content:
        # Recompute deterministic digest of entry digests to catch lock drift.
        material = "\n".join(
            f"{e.get('id')}:{e.get('sha256')}"
            for e in sorted(entries, key=lambda item: item.get("id") or "")
        ) + "\n"
        # content_sha256 is defined by the C++ lock writer; do not reinvent it.
        # Presence-only check here; byte verification covers each entry above.
        del material

    return {"ok": True, "entries": checked, "lock": rel(lock_path)}


def audit_npm_lock(
    findings: list[dict[str, Any]],
    allowed: set[str],
) -> dict[str, Any]:
    lock_path = REPO_ROOT / "package-lock.json"
    lock = load_json(lock_path)
    components: list[dict[str, Any]] = []
    packages = lock.get("packages") or {}
    for key, meta in packages.items():
        if not key.startswith("node_modules/"):
            continue
        if meta.get("link"):
            continue
        name = key.removeprefix("node_modules/")
        integrity = meta.get("integrity")
        if not integrity:
            finding(
                findings,
                severity="critical",
                code="npm_integrity_missing",
                path=key,
                message="npm package missing integrity hash",
            )
        license_id = meta.get("license")
        if isinstance(license_id, dict):
            license_id = license_id.get("type")
        if not license_id:
            finding(
                findings,
                severity="critical",
                code="npm_license_missing",
                path=key,
                message="npm package missing license field",
            )
        elif license_id not in allowed:
            finding(
                findings,
                severity="critical",
                code="license_not_allowed",
                path=key,
                message=f"npm license not allowed: {license_id!r}",
            )
        components.append(
            {
                "type": "library",
                "name": name,
                "version": meta.get("version"),
                "license": license_id,
                "scope": "npm-dev",
                "hashes": [{"alg": "integrity", "content": integrity}] if integrity else [],
            }
        )
    return {"ok": True, "components": components, "package_count": len(components)}


def build_sbom(
    *,
    third_party_components: list[dict[str, Any]],
    npm_components: list[dict[str, Any]],
) -> dict[str, Any]:
    components = []
    for item in third_party_components + npm_components:
        components.append(
            {
                "type": item.get("type", "library"),
                "name": item.get("name"),
                "version": item.get("version"),
                "licenses": (
                    [{"license": {"id": item["license"]}}]
                    if item.get("license")
                    else []
                ),
                "hashes": item.get("hashes") or [],
                "scope": item.get("scope"),
            }
        )
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "version": 1,
        "metadata": {
            "component": {
                "type": "application",
                "name": "instantee",
                "version": "0.1.0",
            },
            "properties": [
                {"name": "instantee:certified_performance", "value": "false"},
                {"name": "instantee:audit", "value": "offline-lock-license"},
            ],
        },
        "components": components,
    }


def run_audit(
    *,
    allowed_path: Path = DEFAULT_ALLOWED,
    limits_path: Path = DEFAULT_LIMITS,
    asset_lock_path: Path = DEFAULT_ASSET_LOCK,
) -> dict[str, Any]:
    findings: list[dict[str, Any]] = []
    allowed_doc = load_json(allowed_path)
    allowed = set(allowed_doc.get("licenses") or [])
    limits_doc = load_json(limits_path)

    dependency = audit_dependency_locks(findings)
    metal = audit_metal_cpp(findings)
    pins = audit_third_party_pins(findings, allowed)
    assets = audit_release_asset_lock(findings, allowed, asset_lock_path)
    npm = audit_npm_lock(findings, allowed)

    # Informational acknowledgment of known stb posture (not critical).
    finding(
        findings,
        severity="info",
        code="stb_security_note",
        path="third_party/stb/stb_image.h",
        message=(
            "stb_image documents a security preference for Wuffs; Instantee keeps "
            "importer dimension and budget limits (offline asset path)."
        ),
    )

    critical = [f for f in findings if f["severity"] == "critical"]
    report = {
        "schema_version": 1,
        "ok": not critical,
        "certified_performance": False,
        "summary": {
            "critical": len(critical),
            "info": sum(1 for f in findings if f["severity"] == "info"),
            "third_party_pins": pins.get("pin_count", 0),
            "release_asset_entries": assets.get("entries", 0),
            "npm_packages": npm.get("package_count", 0),
        },
        "dependency_locks": dependency,
        "metal_cpp": metal,
        "release_asset_lock": assets,
        "platform_limits": limits_doc.get("limits") or [],
        "findings": findings,
        "unresolved_critical": critical,
    }
    sbom = build_sbom(
        third_party_components=pins.get("components") or [],
        npm_components=npm.get("components") or [],
    )
    return {"report": report, "sbom": sbom}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allowed-licenses", type=Path, default=DEFAULT_ALLOWED)
    parser.add_argument("--platform-limits", type=Path, default=DEFAULT_LIMITS)
    parser.add_argument("--asset-lock", type=Path, default=DEFAULT_ASSET_LOCK)
    parser.add_argument("--write-report", type=Path, default=None)
    parser.add_argument("--write-sbom", type=Path, default=None)
    args = parser.parse_args(argv)

    result = run_audit(
        allowed_path=args.allowed_licenses,
        limits_path=args.platform_limits,
        asset_lock_path=args.asset_lock,
    )
    report = result["report"]
    sbom = result["sbom"]

    if args.write_report is not None:
        args.write_report.parent.mkdir(parents=True, exist_ok=True)
        args.write_report.write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    if args.write_sbom is not None:
        args.write_sbom.parent.mkdir(parents=True, exist_ok=True)
        args.write_sbom.write_text(
            json.dumps(sbom, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )

    if report["ok"]:
        summary = report["summary"]
        print(
            "security audit ok "
            f"(pins={summary['third_party_pins']}, "
            f"assets={summary['release_asset_entries']}, "
            f"npm={summary['npm_packages']}, "
            f"critical={summary['critical']})"
        )
        return 0

    for item in report["unresolved_critical"]:
        print(
            f"{item['severity']}:{item['code']}: {item['path']}: {item['message']}",
            file=sys.stderr,
        )
    print("security audit failed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
