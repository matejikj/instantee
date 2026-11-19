#!/usr/bin/env python3
"""Lint the frozen C++ public header surface and CMake visibility rules.

Detects:
  - forbidden Apple/Metal/third-party imports in public headers
  - Apple/Metal type tokens leaking into domain public headers
  - unintended public headers (impl/native/owner/detail naming)
  - Apple frameworks or metal-cpp exposed via PUBLIC CMake edges
  - drift against the locked public-header snapshot
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GRAPH = REPO_ROOT / "cmake" / "module_graph.json"
DEFAULT_SNAPSHOT = REPO_ROOT / "contracts" / "cpp" / "public-headers-v1.snapshot.json"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
LINK_RE = re.compile(
    r"target_link_libraries\s*\(\s*(?P<target>[A-Za-z0-9_]+)\b(?P<body>.*?)\)",
    re.DOTALL,
)
INCLUDE_DIRS_RE = re.compile(
    r"target_include_directories\s*\(\s*(?P<target>[A-Za-z0-9_]+)\b(?P<body>.*?)\)",
    re.DOTALL,
)
FIND_LIBRARY_RE = re.compile(
    r"find_library\s*\(\s*(?P<var>[A-Za-z0-9_]+)\s+(?P<name>[A-Za-z0-9_]+)",
)

FORBIDDEN_INCLUDE_PREFIXES = (
    "Metal/",
    "Foundation/",
    "AVFoundation/",
    "CoreVideo/",
    "CoreMedia/",
    "CoreFoundation/",
    "IOSurface/",
    "AppKit/",
    "QuartzCore/",
    "objc/",
)
FORBIDDEN_INCLUDE_NAMES = (
    "yyjson.h",
    "yyjson/",
    "stb_image.h",
    "stb_image_write.h",
    "nanosvg.h",
    "nanosvgrast.h",
    "webp/decode.h",
    "webp/encode.h",
    "ft2build.h",
    "hb.h",
    "harfbuzz/",
    "tesselator.h",
)
FORBIDDEN_TOKEN_RE = re.compile(
    r"\b(?:"
    r"MTL::|NS::|"
    r"CVPixelBuffer(?:Ref)?|CVMetalTexture(?:Ref)?|"
    r"IOSurfaceRef|CF[A-Z][A-Za-z0-9]+Ref|"
    r"@interface|@protocol|@implementation|"
    r"id\s*<"
    r")\b"
)
UNINTENDED_PUBLIC_NAME_RE = re.compile(
    r"(?:_impl|_native|_owner|_private)\.hpp$|/(?:detail|internal)/",
    re.IGNORECASE,
)
APPLE_FRAMEWORK_NAMES = {
    "Metal",
    "Foundation",
    "CoreFoundation",
    "CoreVideo",
    "CoreMedia",
    "AVFoundation",
    "IOSurface",
    "AppKit",
    "QuartzCore",
}
PLATFORM_MODULES = frozenset(
    {"render-metal", "export-apple", "transfer-apple", "telemetry-apple"}
)


def load_graph(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def module_for_public_header(graph: dict, rel_path: str) -> str | None:
    include = rel_path.split("/include/", 1)[-1]
    for name, module in graph["modules"].items():
        prefix = module.get("include_prefix")
        if prefix and include.startswith(prefix):
            return name
    return None


def is_domain_module(name: str | None) -> bool:
    return name is not None and name not in PLATFORM_MODULES


def strip_comments(text: str) -> str:
    without_block = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*?$", "", without_block, flags=re.MULTILINE)


def iter_public_headers(repo: Path) -> list[Path]:
    headers: list[Path] = []
    for path in (repo / "engine").rglob("*"):
        if path.suffix not in {".hpp", ".h"} or not path.is_file():
            continue
        rel = path.relative_to(repo).as_posix()
        if "/include/instantee/" in rel:
            headers.append(path)
    return sorted(headers, key=lambda item: item.relative_to(repo).as_posix())


def iter_private_headers(repo: Path) -> list[Path]:
    headers: list[Path] = []
    for path in (repo / "engine").rglob("*"):
        if path.suffix not in {".hpp", ".h"} or not path.is_file():
            continue
        rel = path.relative_to(repo).as_posix()
        if "/src/" in rel and "/include/" not in rel:
            headers.append(path)
    return headers


def forbidden_include(include: str) -> str | None:
    if include.startswith(FORBIDDEN_INCLUDE_PREFIXES):
        return include
    for name in FORBIDDEN_INCLUDE_NAMES:
        if include == name or include.startswith(name):
            return include
    if "metal-cpp" in include or include.endswith("Metal/Metal.hpp"):
        return include
    return None


def validate_headers(graph: dict, repo: Path) -> list[str]:
    errors: list[str] = []
    for path in iter_public_headers(repo):
        rel = path.relative_to(repo).as_posix()
        module = module_for_public_header(graph, rel)
        text = path.read_text(encoding="utf-8")
        for include in INCLUDE_RE.findall(text):
            bad = forbidden_include(include)
            if bad is not None:
                errors.append(f"{rel}: forbidden public include <{bad}>")
        if UNINTENDED_PUBLIC_NAME_RE.search(rel):
            errors.append(
                f"{rel}: unintended public header name "
                "(impl/native/owner/detail headers must stay under src/)"
            )
        if is_domain_module(module):
            code = strip_comments(text)
            match = FORBIDDEN_TOKEN_RE.search(code)
            if match:
                errors.append(
                    f"{rel}: domain public header leaks platform token "
                    f"{match.group(0)!r}"
                )
    # Private basenames must not also appear under the public include tree.
    public_by_name = {
        path.name: path.relative_to(repo).as_posix()
        for path in iter_public_headers(repo)
    }
    for path in iter_private_headers(repo):
        if path.name in public_by_name and UNINTENDED_PUBLIC_NAME_RE.search(path.name):
            errors.append(
                f"{public_by_name[path.name]}: private implementation header "
                f"{path.name} must not be published under include/"
            )
    return errors


def split_visibility_tokens(body: str) -> dict[str, list[str]]:
    """Partition CMake dependency tokens by the active PUBLIC/PRIVATE/INTERFACE."""
    current = "PRIVATE"
    buckets: dict[str, list[str]] = {"PUBLIC": [], "PRIVATE": [], "INTERFACE": []}
    for token in re.findall(r'"[^"]+"|[^\s]+', body):
        bare = token.strip('"')
        if bare in {"PUBLIC", "PRIVATE", "INTERFACE"}:
            current = bare
            continue
        if bare.startswith("#"):
            continue
        buckets[current].append(bare)
    return buckets


def apple_framework_vars(cmake_text: str) -> set[str]:
    found: set[str] = set()
    for match in FIND_LIBRARY_RE.finditer(cmake_text):
        if match.group("name") in APPLE_FRAMEWORK_NAMES:
            found.add(match.group("var"))
            found.add(f"${{{match.group('var')}}}")
    return found


def validate_cmake(graph: dict, repo: Path) -> list[str]:
    errors: list[str] = []
    target_to_module = {
        module["target"]: name
        for name, module in graph["modules"].items()
        if module.get("target")
    }
    for path in (repo / "engine").rglob("CMakeLists.txt"):
        text = path.read_text(encoding="utf-8")
        rel = path.relative_to(repo).as_posix()
        framework_vars = apple_framework_vars(text)

        for match in LINK_RE.finditer(text):
            target = match.group("target")
            module = target_to_module.get(target)
            buckets = split_visibility_tokens(match.group("body"))
            for token in buckets["PUBLIC"]:
                if token in framework_vars or token in APPLE_FRAMEWORK_NAMES:
                    errors.append(
                        f"{rel}: {target} links Apple framework {token} as PUBLIC"
                    )
                if "metal-cpp" in token or "Metal/Metal.hpp" in token:
                    errors.append(
                        f"{rel}: {target} must not PUBLIC-link metal-cpp ({token})"
                    )
            if module and is_domain_module(module):
                for section, tokens in buckets.items():
                    for token in tokens:
                        if token in framework_vars or token in APPLE_FRAMEWORK_NAMES:
                            errors.append(
                                f"{rel}: domain module {module} must not link "
                                f"Apple framework {token} ({section})"
                            )

        for match in INCLUDE_DIRS_RE.finditer(text):
            target = match.group("target")
            buckets = split_visibility_tokens(match.group("body"))
            for token in buckets["PUBLIC"]:
                if "metal-cpp" in token or "/src/" in token.replace("\\", "/"):
                    errors.append(
                        f"{rel}: {target} exposes private include path via PUBLIC "
                        f"({token})"
                    )

        # Domain modules must not probe Apple frameworks at all.
        module_dir = None
        for name, module in graph["modules"].items():
            target = module.get("target")
            if not target:
                continue
            # Match owning CMakeLists by directory stem.
            stem = name
            if (
                f"engine/src/{stem}/CMakeLists.txt" == rel
                or f"engine/native/{stem}/CMakeLists.txt" == rel
            ):
                module_dir = name
                break
        if module_dir and is_domain_module(module_dir):
            for match in FIND_LIBRARY_RE.finditer(text):
                if match.group("name") in APPLE_FRAMEWORK_NAMES:
                    errors.append(
                        f"{rel}: domain module {module_dir} must not find_library "
                        f"{match.group('name')}"
                    )
    return errors


def collect_snapshot(repo: Path) -> dict:
    headers = [
        path.relative_to(repo).as_posix() for path in iter_public_headers(repo)
    ]
    return {
        "schema_version": "1.0.0",
        "description": "Frozen inventory of C++ public headers under engine/**/include.",
        "header_count": len(headers),
        "headers": headers,
    }


def validate_snapshot(repo: Path, snapshot_path: Path) -> list[str]:
    errors: list[str] = []
    actual = collect_snapshot(repo)
    if not snapshot_path.exists():
        return [f"missing public-header snapshot: {snapshot_path}"]
    expected = json.loads(snapshot_path.read_text(encoding="utf-8"))
    if actual["headers"] != expected.get("headers"):
        actual_set = set(actual["headers"])
        expected_set = set(expected.get("headers", []))
        for missing in sorted(expected_set - actual_set):
            errors.append(f"public header removed without snapshot update: {missing}")
        for added in sorted(actual_set - expected_set):
            errors.append(f"unintended public header (not in snapshot): {added}")
    if actual["header_count"] != expected.get("header_count"):
        errors.append(
            "public header count drifted: "
            f"expected {expected.get('header_count')}, got {actual['header_count']}"
        )
    return errors


def validate(graph: dict, repo: Path, snapshot_path: Path) -> list[str]:
    return sorted(
        set(
            validate_headers(graph, repo)
            + validate_cmake(graph, repo)
            + validate_snapshot(repo, snapshot_path)
        )
    )


def write_report(path: Path, *, errors: list[str], snapshot: dict) -> None:
    payload = {
        "schema_version": 1,
        "result": "passed" if not errors else "failed",
        "public_header_count": snapshot["header_count"],
        "platform_modules": sorted(PLATFORM_MODULES),
        "checks": [
            "forbidden Apple/Metal/third-party includes in public headers",
            "Apple/Metal tokens in domain public headers",
            "unintended impl/native/owner/detail public headers",
            "Apple frameworks and metal-cpp kept PRIVATE in CMake",
            "locked public-header snapshot",
        ],
        "error_count": len(errors),
        "errors": errors,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graph", type=Path, default=DEFAULT_GRAPH)
    parser.add_argument("--repo", type=Path, default=REPO_ROOT)
    parser.add_argument("--snapshot", type=Path, default=DEFAULT_SNAPSHOT)
    parser.add_argument(
        "--update-snapshot",
        action="store_true",
        help="rewrite the locked public-header inventory after an intentional change",
    )
    parser.add_argument(
        "--report",
        type=Path,
        help="write a machine-readable API review report",
    )
    args = parser.parse_args(argv)
    repo = args.repo.resolve()
    graph = load_graph(args.graph)
    snapshot = collect_snapshot(repo)
    if args.update_snapshot:
        args.snapshot.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot.write_text(
            json.dumps(snapshot, indent=2) + "\n", encoding="utf-8"
        )
        print(f"updated {args.snapshot.relative_to(repo)}")
        return 0

    errors = validate(graph, repo, args.snapshot.resolve())
    if args.report is not None:
        write_report(args.report.resolve(), errors=errors, snapshot=snapshot)
    if errors:
        print("C++ API surface violations:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(
        f"C++ API surface ok ({snapshot['header_count']} public headers, "
        "snapshot matched)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
