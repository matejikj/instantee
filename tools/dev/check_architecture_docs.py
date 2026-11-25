#!/usr/bin/env python3
"""Verify implementation architecture docs cover modules, ADRs and style guidance.

Fails when the module catalog drifts from cmake/module_graph.json, when the ADR
index misses a required ID, or when the style-extension guide would send
contributors into GPU/metal-cpp internals for ordinary style work.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GRAPH = REPO_ROOT / "cmake" / "module_graph.json"
DOCS = REPO_ROOT / "architecture"
REQUIRED_DOCS = (
    "README.md",
    "modules.md",
    "data-flow.md",
    "ownership-and-jobs.md",
    "extending-styles.md",
    "adr-index.md",
)
REQUIRED_ADRS = [f"ADR-{i:04d}" for i in range(1, 16)]
STYLE_GUIDE_FORBIDDEN = (
    "metal-cpp",
    "MTLTexture",
    "MTLCommandBuffer",
    "id<MTL",
    "CVPixelBuffer",
)
# Mentions in the "do not touch" section are allowed when clearly negated.
STYLE_GUIDE_FORBIDDEN_CONTEXT_OK = re.compile(
    r"(?i)(must not|do not|without|never|not to touch|not open|not embed).{0,80}"
    r"(metal-cpp|MTL|CVPixelBuffer|IOSurface)"
)


def load_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def check_required_files() -> list[str]:
    errors: list[str] = []
    if not (REPO_ROOT / "ARCHITECTURE.md").is_file():
        errors.append("missing ARCHITECTURE.md")
    for name in REQUIRED_DOCS:
        if not (DOCS / name).is_file():
            errors.append(f"missing architecture/{name}")
    arch = REPO_ROOT / "ARCHITECTURE.md"
    if arch.is_file():
        text = load_text(arch)
        for name in ("modules.md", "extending-styles.md", "adr-index.md"):
            if f"architecture/{name}" not in text and name not in text:
                errors.append(f"ARCHITECTURE.md must link to architecture/{name}")
    return errors


def check_module_catalog(graph: dict) -> list[str]:
    errors: list[str] = []
    catalog = load_text(DOCS / "modules.md")
    for name, module in graph.get("modules", {}).items():
        if f"`{name}`" not in catalog:
            errors.append(f"modules.md missing module `{name}`")
            continue
        include_prefix = module.get("include_prefix") or ""
        if include_prefix and f"`{include_prefix}`" not in catalog:
            errors.append(
                f"modules.md missing include prefix for `{name}`: {include_prefix}"
            )
        # Path heuristics used in the catalog.
        if name.endswith("-apple"):
            expected = f"engine/native/{name}/"
        else:
            expected = f"engine/src/{name}/"
        if f"`{expected}`" not in catalog:
            errors.append(f"modules.md missing path for `{name}`: {expected}")
    return errors


def check_adr_index() -> list[str]:
    errors: list[str] = []
    text = load_text(DOCS / "adr-index.md")
    for adr in REQUIRED_ADRS:
        if adr not in text:
            errors.append(f"adr-index.md missing {adr}")
    if "contracts/adrs/0013-scene-ir-evolution-policy.md" not in text:
        errors.append("adr-index.md must reference versioned ADR-0013 path")
    return errors


def check_style_guide() -> list[str]:
    errors: list[str] = []
    text = load_text(DOCS / "extending-styles.md")
    lowered = text.lower()
    for needle in (
        "style bible",
        "direction",
        "render-metal",
        "style_packages",
        "safe_area",
    ):
        if needle not in lowered:
            errors.append(f"extending-styles.md should mention {needle!r}")

    # Positive guidance must point at Style Bible / assets / authoring — not
    # require reading GPU handle APIs. Forbidden tokens may appear only in a
    # negated "do not touch" context.
    for token in STYLE_GUIDE_FORBIDDEN:
        for match in re.finditer(re.escape(token), text):
            start = max(0, match.start() - 80)
            window = text[start : match.end() + 20]
            if STYLE_GUIDE_FORBIDDEN_CONTEXT_OK.search(window):
                continue
            # Also allow fenced "do not touch" bullet lists containing the path.
            line_start = text.rfind("\n", 0, match.start()) + 1
            line = text[line_start : text.find("\n", match.start())]
            if line.lstrip().startswith("-") and (
                "not" in line.lower() or "metal-cpp" in line or "MTL" in line
            ):
                # Listed under avoidance guidance.
                if any(
                    marker in text[max(0, line_start - 200) : line_start].lower()
                    for marker in ("not to touch", "do not", "without")
                ) or "What not to touch" in text[:line_start]:
                    continue
            errors.append(
                "extending-styles.md mentions GPU/handle token outside avoidance "
                f"guidance: {token}"
            )
    return errors


def check_ownership_doc() -> list[str]:
    errors: list[str] = []
    text = load_text(DOCS / "ownership-and-jobs.md")
    for phase in (
        "created",
        "preparing",
        "rendering",
        "finishing",
        "publishing",
        "published",
        "cancelled",
    ):
        if phase not in text:
            errors.append(f"ownership-and-jobs.md missing RenderJob phase {phase!r}")
    if "RenderJob" not in text:
        errors.append("ownership-and-jobs.md must name RenderJob")
    return errors


def run_checks(graph_path: Path = DEFAULT_GRAPH) -> list[str]:
    errors: list[str] = []
    errors.extend(check_required_files())
    if errors:
        return errors
    graph = json.loads(graph_path.read_text(encoding="utf-8"))
    errors.extend(check_module_catalog(graph))
    errors.extend(check_adr_index())
    errors.extend(check_style_guide())
    errors.extend(check_ownership_doc())
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graph", type=Path, default=DEFAULT_GRAPH)
    args = parser.parse_args(argv)
    errors = run_checks(args.graph)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        print("architecture docs check failed", file=sys.stderr)
        return 1
    module_count = len(json.loads(args.graph.read_text(encoding="utf-8"))["modules"])
    print(f"architecture docs ok (modules={module_count}, adrs={len(REQUIRED_ADRS)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
