#!/usr/bin/env python3
"""Offline checksum audit for versioned B-01..B-05 workload locks."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from workload_lock import (
    CATALOG_PATH,
    REPO_ROOT,
    WorkloadLockError,
    verify_catalog,
    verify_workload_lock,
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--catalog",
        type=Path,
        default=CATALOG_PATH,
        help="benchmark workload catalog JSON",
    )
    parser.add_argument(
        "--lock",
        type=Path,
        action="append",
        default=[],
        help="verify a single workload lock (repeatable); skips catalog digest pin",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="print machine-readable verification report",
    )
    args = parser.parse_args(argv)

    try:
        if args.lock:
            reports = [
                verify_workload_lock(
                    path if path.is_absolute() else REPO_ROOT / path,
                    root=REPO_ROOT,
                )
                for path in args.lock
            ]
            payload = {"ok": True, "workloads": reports}
        else:
            payload = verify_catalog(root=REPO_ROOT, catalog_path=args.catalog)
    except WorkloadLockError as exc:
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(f"ok catalog_sha256={payload.get('catalog_sha256', 'n/a')}")
        for row in payload["workloads"]:
            print(
                f"  {row['workload_id']}: entry={row['entry_digest'][:16]}… "
                f"frames={row['duration_frames']} tier={row['quality_tier']}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
