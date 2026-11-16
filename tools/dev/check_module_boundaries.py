#!/usr/bin/env python3
"""Enforce the native module dependency graph and spike isolation rules."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GRAPH = REPO_ROOT / "cmake" / "module_graph.json"

INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"](instantee/[^>"]+)[>"]', re.MULTILINE
)
LINK_RE = re.compile(
    r"target_link_libraries\s*\(\s*(?P<target>[A-Za-z0-9_]+)\b(?P<body>.*?)\)",
    re.DOTALL,
)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".m", ".mm"}


def load_graph(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def module_by_target(graph: dict) -> dict[str, str]:
    mapping = {}
    for name, module in graph["modules"].items():
        target = module.get("target")
        if target:
            mapping[target] = name
            mapping[f"instantee::{name.replace('-', '_')}"] = name
    # Prefer alias names used in the tree.
    mapping.update(
        {
            "instantee::diagnostics": "diagnostics",
            "instantee::core_model": "core-model",
            "instantee::runtime": "runtime",
            "instantee::export_core": "export-core",
            "instantee::render_metal": "render-metal",
            "instantee::export_apple": "export-apple",
            "instantee::telemetry_apple": "telemetry-apple",
            "instantee::transfer_apple": "transfer-apple",
        }
    )
    return mapping


def include_to_module(graph: dict, include: str) -> str | None:
    for name, module in graph["modules"].items():
        prefix = module.get("include_prefix")
        if prefix and include.startswith(prefix):
            return name
    return None


def production_modules(graph: dict) -> set[str]:
    return {name for name, module in graph["modules"].items() if module["role"] == "production"}


def parse_link_edges(cmake_text: str, target_map: dict[str, str]) -> list[tuple[str, str]]:
    edges: list[tuple[str, str]] = []
    for match in LINK_RE.finditer(cmake_text):
        target = match.group("target")
        source = target_map.get(target)
        if source is None:
            continue
        body = match.group("body")
        for token in re.findall(r"[A-Za-z0-9_:<>-]+", body):
            if token in {"PUBLIC", "PRIVATE", "INTERFACE", "instantee_build_options"}:
                continue
            if token.startswith("${") or token.startswith('"'):
                continue
            dep = target_map.get(token)
            if dep is not None and dep != source:
                edges.append((source, dep))
    return edges


def scan_cmake_edges(root: Path, target_map: dict[str, str]) -> list[tuple[str, str]]:
    edges: list[tuple[str, str]] = []
    for path in root.rglob("CMakeLists.txt"):
        edges.extend(parse_link_edges(path.read_text(encoding="utf-8"), target_map))
    return edges


def scan_includes(root: Path) -> dict[str, set[str]]:
    """Map repository-relative source path → included instantee headers."""
    found: dict[str, set[str]] = {}
    for path in root.rglob("*"):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        includes = set(INCLUDE_RE.findall(text))
        if includes:
            found[rel] = includes
    return found


def source_module(graph: dict, rel_path: str) -> str | None:
    full = rel_path if rel_path.startswith("engine/") else f"engine/{rel_path}"
    for name, module in graph["modules"].items():
        # Match owning directory by include prefix stem under engine/.
        stem = name
        candidates = (
            f"engine/src/{stem}/",
            f"engine/native/{stem}/",
        )
        if any(full.startswith(candidate) for candidate in candidates):
            return name
    return None


def validate(graph: dict, repo: Path) -> list[str]:
    errors: list[str] = []
    target_map = module_by_target(graph)
    allowed = {
        name: set(module.get("allowed_dependencies", []))
        for name, module in graph["modules"].items()
    }
    prod = production_modules(graph)
    spike_prefixes = tuple(graph["spike_include_prefixes"])

    for source, dep in scan_cmake_edges(repo / "engine", target_map):
        if dep not in allowed.get(source, set()):
            errors.append(f"CMake edge {source} → {dep} is not allowed")
        if source in prod and graph["modules"][dep]["role"] == "spike":
            errors.append(f"production module {source} must not link spike module {dep}")

    includes_by_file = scan_includes(repo / "engine")
    for rel, includes in includes_by_file.items():
        module = source_module(graph, f"engine/{rel}")
        if module is None:
            # Applications and tests may compose spike modules.
            continue
        for include in includes:
            dep = include_to_module(graph, include)
            if dep is None or dep == module:
                continue
            if dep not in allowed.get(module, set()):
                errors.append(
                    f"{rel}: include <{include}> pulls in disallowed module {dep}"
                )
            if module in prod and any(include.startswith(prefix) for prefix in spike_prefixes):
                errors.append(
                    f"{rel}: production module {module} includes spike header <{include}>"
                )
            if module in prod and "instantee/spike" in include:
                errors.append(f"{rel}: production module references spike path <{include}>")

    # Production sources must not name the spike diagnostic namespace.
    for root in graph["production_source_roots"]:
        base = repo / root
        if not base.exists():
            errors.append(f"missing production source root {root}")
            continue
        for path in base.rglob("*"):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8")
            if "instantee::spike" in text or re.search(r"\bspike::", text):
                rel = path.relative_to(repo).as_posix()
                errors.append(
                    f"{rel}: production source must not reference the spike namespace"
                )
    return sorted(set(errors))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graph", type=Path, default=DEFAULT_GRAPH)
    parser.add_argument("--repo", type=Path, default=REPO_ROOT)
    args = parser.parse_args(argv)
    graph = load_graph(args.graph)
    errors = validate(graph, args.repo.resolve())
    if errors:
        print("module boundary violations:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("module boundaries ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
