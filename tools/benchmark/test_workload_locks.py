"""Behavioral checks for versioned benchmark workload lock audits."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from workload_lock import (
    CATALOG_PATH,
    REPO_ROOT,
    WorkloadLockError,
    load_catalog,
    sha256_file,
    verify_catalog,
    verify_workload_lock,
)


class WorkloadLockAuditTest(unittest.TestCase):
    def test_catalog_lists_b01_through_b05(self):
        catalog = load_catalog()
        families = [row["family"] for row in catalog["workloads"]]
        self.assertEqual(families, ["B-01", "B-02", "B-03", "B-04", "B-05"])

    def test_catalog_verifies_offline(self):
        report = verify_catalog()
        self.assertTrue(report["ok"])
        self.assertEqual(len(report["workloads"]), 5)
        self.assertEqual(report["catalog_sha256"], load_catalog()["catalog_sha256"])

    def test_locked_pre_measurement_for_mixed_and_worst(self):
        catalog = load_catalog()
        by_family = {row["family"]: row for row in catalog["workloads"]}
        self.assertEqual(by_family["B-04"]["status"], "locked_pre_measurement")
        self.assertEqual(by_family["B-05"]["status"], "locked_pre_measurement")
        self.assertEqual(by_family["B-04"]["duration_frames"], 9000)
        self.assertEqual(by_family["B-05"]["duration_frames"], 9000)

    def test_scene_digest_drift_requires_new_baseline(self):
        lock_path = REPO_ROOT / "contracts/fixtures/b01-editorial/b01-workload.lock.json"
        lock = json.loads(lock_path.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scene_rel = Path(lock["scene"])
            scene_dst = root / scene_rel
            scene_dst.parent.mkdir(parents=True, exist_ok=True)
            original = (REPO_ROOT / scene_rel).read_bytes()
            scene_dst.write_bytes(original + b"\n")
            # Copy remaining pinned files unchanged.
            for rel in (lock["asset_lock"], lock["style_bible"]):
                dst = root / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes((REPO_ROOT / rel).read_bytes())
            poisoned = root / "lock.json"
            poisoned.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaises(WorkloadLockError) as ctx:
                verify_workload_lock(poisoned, root=root)
            message = str(ctx.exception)
            self.assertIn("bump workload_id", message)
            self.assertIn(lock["workload_id"], message)

    def test_unchanged_lock_still_matches_disk(self):
        lock_path = REPO_ROOT / "contracts/fixtures/b03-paper/b03-workload.lock.json"
        report = verify_workload_lock(lock_path)
        scene = REPO_ROOT / report["scene"]
        self.assertEqual(sha256_file(scene), report["scene_sha256"])

    def test_catalog_path_stable(self):
        self.assertTrue(CATALOG_PATH.is_file())
        self.assertEqual(
            CATALOG_PATH.relative_to(REPO_ROOT).as_posix(),
            "contracts/fixtures/benchmark-workloads/catalog-v1.json",
        )


if __name__ == "__main__":
    unittest.main()
