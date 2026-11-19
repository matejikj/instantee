"""Behavioral checks for performance-report schema + semantic harness."""

from __future__ import annotations

import copy
import importlib.util
import json
import unittest
from pathlib import Path

from performance_report import (
    PerformanceReportError,
    collect_semantic_issues,
    default_schema_path,
    is_certification_eligible,
    validate_performance_report,
    validate_structure,
)

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "contracts" / "fixtures" / "performance-report"


def load(relative: str) -> dict:
    return json.loads((FIXTURES / relative).read_text(encoding="utf-8"))


class PerformanceReportHarnessTest(unittest.TestCase):
    def test_default_schema_prefers_contracts_copy(self):
        path = default_schema_path()
        self.assertTrue(path.is_file())
        self.assertEqual(path, ROOT / "contracts/schemas/performance-report-v1.schema.json")

    def test_schema_fixture_is_not_certification_evidence(self):
        report = load("positive/schema-fixture.json")
        validate_performance_report(report)
        self.assertEqual(report["evidence_kind"], "schema_fixture")
        self.assertIsNone(report["metrics"])
        self.assertFalse(is_certification_eligible(report))

    def test_measured_success_is_semantically_consistent(self):
        report = load("positive/measured-success-min.json")
        meta = validate_performance_report(report)
        self.assertIn(meta["backend"], {"jsonschema", "fallback"})
        self.assertTrue(is_certification_eligible(report))
        self.assertIs(report["certified"], False)

    def test_schema_negative_missing_metrics(self):
        report = load("negative/missing-metrics.json")
        with self.assertRaises(PerformanceReportError):
            validate_structure(report)

    def test_semantic_negatives_invalidate_certification(self):
        cases = sorted((FIXTURES / "semantic-negative").glob("*.json"))
        self.assertGreaterEqual(len(cases), 6)
        for path in cases:
            with self.subTest(path.name):
                report = json.loads(path.read_text(encoding="utf-8"))
                # Fixtures remain structurally acceptable under the fallback/shape rules
                # that mirror success-object requirements except for the injected lie.
                issues = collect_semantic_issues(report)
                self.assertTrue(issues, path.name)
                self.assertFalse(is_certification_eligible(report))
                with self.assertRaises(PerformanceReportError):
                    validate_performance_report(report)

    def test_measured_distinguished_from_fixture_mutation(self):
        fixture = load("positive/schema-fixture.json")
        claimed = copy.deepcopy(fixture)
        claimed["evidence_kind"] = "measured"
        claimed["result"] = "success"
        claimed["certified"] = False
        # Still missing measured profiles/metrics → not eligible and not valid.
        self.assertFalse(is_certification_eligible(claimed))
        self.assertTrue(collect_semantic_issues(claimed))

    def test_single_run_cannot_self_certify(self):
        report = load("positive/measured-success-min.json")
        report["certified"] = True
        issues = collect_semantic_issues(report)
        self.assertTrue(any("certified" in item for item in issues))
        self.assertFalse(is_certification_eligible(report))

    @unittest.skipUnless(
        importlib.util.find_spec("jsonschema") is not None,
        "optional jsonschema package required for Draft 2020-12 backend",
    )
    def test_jsonschema_backend_accepts_positive_fixtures(self):
        for relative in (
            "positive/schema-fixture.json",
            "positive/measured-success-min.json",
        ):
            with self.subTest(relative):
                meta = validate_structure(load(relative), require_jsonschema=True)
                self.assertEqual(meta["backend"], "jsonschema")

    @unittest.skipUnless(
        importlib.util.find_spec("jsonschema") is not None,
        "optional jsonschema package required for Draft 2020-12 backend",
    )
    def test_jsonschema_backend_rejects_schema_negative(self):
        with self.assertRaises(PerformanceReportError):
            validate_structure(load("negative/missing-metrics.json"), require_jsonschema=True)

    @unittest.skipUnless(
        importlib.util.find_spec("jsonschema") is not None,
        "optional jsonschema package required for Draft 2020-12 backend",
    )
    def test_semantic_negatives_remain_schema_valid(self):
        for path in sorted((FIXTURES / "semantic-negative").glob("*.json")):
            with self.subTest(path.name):
                report = json.loads(path.read_text(encoding="utf-8"))
                validate_structure(report, require_jsonschema=True)
                self.assertTrue(collect_semantic_issues(report))


if __name__ == "__main__":
    unittest.main()
