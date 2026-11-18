"""Unit tests for the SAS §2 requirements coverage assembler."""

from __future__ import annotations

import unittest

import requirements_coverage as coverage


class RequirementsCoverageTests(unittest.TestCase):
    def test_parse_sas_priorities_marks_stretch_and_should(self):
        sas = """
## 2. Funkční a nefunkční požadavky
| FR-01 | Definice filmu z TypeScriptu | MUST | SDK |
| FR-14 | Preview s přeskakováním na časové ose | SHOULD | Random-access |
| NFR-PERF-01 | Throughput | **≥5 fps** |
| NFR-PERF-02 | Interní rezerva | stretch target |
"""
        parsed = coverage.parse_sas_priorities(sas)
        self.assertEqual(parsed["FR-01"]["priority"], "MUST")
        self.assertEqual(parsed["FR-14"]["priority"], "SHOULD")
        self.assertEqual(parsed["NFR-PERF-01"]["priority"], "MUST")
        self.assertEqual(parsed["NFR-PERF-02"]["priority"], "STRETCH")

    def test_parse_rtm_extracts_task_ids(self):
        rtm = """
| Requirement | Požadavek | Planned implementing & verification tasks |
|---|---|---|
| `FR-01` | Definice filmu z TypeScriptu | `P1-010`, `P1-011`, `P8-002` |
| `NFR-DET-02` | Tolerantní golden pixel testy | `P8-027` |
"""
        mapping = coverage.parse_rtm_tasks(rtm)
        self.assertEqual(mapping["FR-01"], ["P1-010", "P1-011", "P8-002"])
        self.assertEqual(mapping["NFR-DET-02"], ["P8-027"])

    def test_build_matrix_flags_uncovered_must_and_untracked_gate(self):
        sas = """
| FR-01 | Definice filmu z TypeScriptu | MUST | SDK |
| FR-14 | Preview | SHOULD | preview |
| NFR-PERF-02 | Interní rezerva | stretch |
"""
        rtm = """
| `FR-14` | Preview | `P2-017` |
| `NFR-PERF-02` | Interní rezerva | `P3-022` |
"""
        backlog = {
            "tasks": [
                {"id": "P2-017", "verification": "preview test"},
                {"id": "P3-022", "verification": "series"},
            ]
        }
        tracker = {
            "current_backlog": {"path": "motion_framework_sas/implementation_tasks.json"},
            "tasks": {
                "P2-017": {
                    "status": "done",
                    "acceptance": {"met": True},
                    "evidence": ["implementation-evidence/P2-017/"],
                    "verification": "preview test",
                },
                "P3-022": {
                    "status": "done",
                    "acceptance": {"met": True},
                    "evidence": ["implementation-evidence/P3-022/"],
                },
            },
            "phase_gates": {
                "P0": {"gate_task_id": "P0-020", "status": "done", "decision": "GO"},
            },
        }
        matrix = coverage.build_matrix(sas, rtm, backlog, tracker)
        self.assertIn("FR-01", matrix["uncovered_must"])
        self.assertTrue(len(matrix["untracked_release_gates"]) >= 1)
        self.assertFalse(matrix["acceptance"]["met"])

    def test_build_matrix_accepts_complete_mapping(self):
        req_ids = [
            "FR-01",
            "FR-14",
            "NFR-PERF-02",
        ]
        sas_lines = [
            "| FR-01 | Definice | MUST | x |",
            "| FR-14 | Preview | SHOULD | x |",
            "| NFR-PERF-02 | Interní rezerva | stretch |",
        ]
        rtm_lines = [
            "| `FR-01` | Definice | `P1-010` |",
            "| `FR-14` | Preview | `P2-017` |",
            "| `NFR-PERF-02` | Interní rezerva | `P3-022` |",
        ]
        backlog = {
            "tasks": [
                {"id": "P1-010", "verification": "sdk"},
                {"id": "P2-017", "verification": "preview"},
                {"id": "P3-022", "verification": "series"},
            ]
        }
        tracker = {
            "tasks": {
                "P1-010": {
                    "status": "done",
                    "acceptance": {"met": True},
                    "evidence": ["e"],
                    "verification": "sdk",
                },
                "P2-017": {
                    "status": "todo",
                    "acceptance": {"met": None},
                    "evidence": [],
                },
                "P3-022": {
                    "status": "done",
                    "acceptance": {"met": True},
                    "evidence": ["e"],
                },
            },
            "phase_gates": {
                phase: {
                    "gate_task_id": gate,
                    "status": "todo" if phase == "P8" else "done",
                    "decision": None if phase == "P8" else "PASS",
                }
                for phase, gate in coverage.EXPECTED_GATES.items()
            },
        }
        matrix = coverage.build_matrix(
            "\n".join(sas_lines), "\n".join(rtm_lines), backlog, tracker
        )
        self.assertEqual(sorted(row["id"] for row in matrix["requirements"]), sorted(req_ids))
        self.assertEqual(matrix["uncovered_must"], [])
        self.assertEqual(matrix["untracked_release_gates"], [])
        self.assertTrue(matrix["acceptance"]["met"])


if __name__ == "__main__":
    unittest.main()
