import copy
import unittest

import sync_tracker

TASKS = [
    {"id": "P1-001", "delivery_slice": "P1-S1"},
    {"id": "P1-002", "delivery_slice": "P1-S1"},
    {"id": "P1-003", "delivery_slice": "P1-S2"},
    {"id": "P2-001", "delivery_slice": None},
]
TRACKER = {
    "current_backlog": {"sha256": "abc"},
    "tasks": {task["id"]: {} for task in TASKS},
    "delivery_slices": {
        "policy": "text",
        "P1": [
            {"id": "P1-S1", "scope": "first", "tasks": ["P1-002", "P1-001"]},
            {"id": "P1-S2", "scope": "second", "tasks": ["P1-003"]},
        ],
    },
}


class FindDriftTest(unittest.TestCase):
    def test_matching_tracker_has_no_drift(self):
        self.assertEqual(sync_tracker.find_drift(TRACKER, TASKS, "abc"), [])

    def test_reports_digest_task_set_and_slice_differences(self):
        tracker = copy.deepcopy(TRACKER)
        del tracker["tasks"]["P2-001"]
        tracker["tasks"]["P9-001"] = {}
        tracker["delivery_slices"]["P1"][1]["tasks"].append("P1-004")
        problems = sync_tracker.find_drift(tracker, TASKS, "def")
        self.assertEqual(len(problems), 4)
        self.assertIn("current_backlog.sha256", problems[0])
        self.assertIn("P2-001", problems[1])
        self.assertIn("P9-001", problems[2])
        self.assertIn("P1-S2", problems[3])

    def test_reports_missing_slices_but_not_listing_order(self):
        tracker = copy.deepcopy(TRACKER)
        tracker["delivery_slices"]["P1"].reverse()
        self.assertEqual(sync_tracker.find_drift(tracker, TASKS, "abc"), [])
        tracker["delivery_slices"]["P1"].pop()
        self.assertIn("slice IDs differ", sync_tracker.find_drift(tracker, TASKS, "abc")[0])


if __name__ == "__main__":
    unittest.main()
