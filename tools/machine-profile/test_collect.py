import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from collect import (Probes, collect, metric, parse_load, parse_power, parse_swap,
                     profiler_projection, reference_identity)
from verify import validate


class CollectionTests(unittest.TestCase):
    def test_gpu_variants_have_distinct_identity(self):
        identities = [reference_identity("MacBookAir10,1", "Apple M1", 17179869184, gpu, 8, 8)
                      for gpu in (7, 8)]
        self.assertTrue(all(item["status"] == "matched" for item in identities))
        self.assertNotEqual(identities[0]["variant"], identities[1]["variant"])

    def test_other_device_cannot_be_reference(self):
        for args in (("Mac14,2", "Apple M2", 17179869184, 8, 8, 8),
                     ("MacBookAir10,1", "Apple M1", 8589934592, 7, 8, 8),
                     ("MacBookAir10,1", "Apple M1", 17179869184, 10, 8, 8)):
            self.assertEqual(reference_identity(*args)["status"], "mismatch")

    def test_missing_measurement_never_matches(self):
        args = ["MacBookAir10,1", "Apple M1", 17179869184, 7, 8, 8]
        for index in range(len(args)):
            missing = args.copy()
            missing[index] = None
            self.assertEqual(reference_identity(*missing)["status"], "unverified")

    def test_battery_and_ac_are_observations(self):
        for source, state in (("Battery Power", "discharging"), ("AC Power", "charging")):
            self.assertEqual(parse_power(f"Now drawing from '{source}'\n -InternalBattery-0 23%; {state}; 0:38 remaining"),
                             {"source": source, "battery_charge_percent": 23, "battery_state": state})

    def test_load_is_runnable_jobs(self):
        self.assertEqual(parse_load("{ 1.60 1.94 2.02 }"),
                         {"1_minute": 1.6, "5_minutes": 1.94, "15_minutes": 2.02})

    def test_swap_binary_units_and_real_zero(self):
        self.assertEqual(parse_swap("total = 1.00G used = 512.00M free = 1024.00K (encrypted)"),
                         {"total": 1073741824, "used": 536870912, "free": 1048576})
        self.assertEqual(parse_swap("total = 0.00M used = 0.00M free = 0.00M")["used"], 0)

    def test_unknown_formats_do_not_invent_values(self):
        for parser in (parse_power, parse_load, parse_swap):
            with self.assertRaises(ValueError):
                parser("unavailable")

    def test_failure_is_null_with_raw_reason(self):
        probes = Probes()
        failed = subprocess.CompletedProcess([], 1, b"", b"Operation not permitted")
        with patch("collect.subprocess.run", return_value=failed):
            result = probes.measure("denied", ["sysctl"], "bytes", int)
        self.assertIsNone(result["value"])
        self.assertIn("Operation not permitted", result["reason"])
        self.assertEqual(probes.records["denied"]["returncode"], 1)

    def test_timeout_remains_unavailable(self):
        with patch("collect.subprocess.run", side_effect=subprocess.TimeoutExpired("sysctl", 45)):
            result = Probes().measure("timeout", ["sysctl"])
        self.assertIsNone(result["value"])
        self.assertIn("timed out", result["reason"])

    def test_unparseable_output_remains_unavailable(self):
        success = subprocess.CompletedProcess([], 0, b"changed format", b"")
        with patch("collect.subprocess.run", return_value=success):
            result = Probes().measure("power", ["pmset"], convert=parse_power)
        self.assertIsNone(result["value"])
        self.assertTrue(result["reason"])

    def test_identifiers_are_excluded_from_persisted_projection(self):
        projected = profiler_projection({"SPHardwareDataType": [{"machine_model": "MacBookAir10,1",
                    "serial_number": "PRIVATE", "platform_UUID": "PRIVATE", "provisioning_UDID": "PRIVATE"}],
                    "SPDisplaysDataType": [{"sppci_cores": "7", "spdisplays_ndrvs": ["PRIVATE"]}]})
        self.assertNotIn("PRIVATE", json.dumps(projected))

    def test_existing_evidence_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "existing.json"
            marker.write_text("keep")
            with patch("collect.platform.system", return_value="Darwin"), self.assertRaises(FileExistsError):
                collect(Path(directory), Path(directory), 2, 1)
            self.assertEqual(marker.read_text(), "keep")


class EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = Path(__file__).resolve().parents[2] / "implementation-evidence/P0-001/reference"
        if not directory.exists():
            raise unittest.SkipTest("Local machine-profile reference evidence is unavailable")
        cls.profile = json.loads((directory / "machine-profile.json").read_text())
        cls.probes = json.loads((directory / "raw/probes.json").read_text())

    def test_recorded_evidence_is_complete(self):
        self.assertEqual(validate(self.profile, self.probes), [])

    def test_missing_thermal_cannot_pass(self):
        profile = copy.deepcopy(self.profile)
        profile["samples"][0]["thermal_state"] = metric(None, "enum", "missing", "API denied")
        self.assertTrue(validate(profile, self.probes))

    def test_ram_disagreement_cannot_pass(self):
        profile = copy.deepcopy(self.profile)
        profile["samples"][0]["physical_memory_bytes"]["value"] = 8589934592
        self.assertTrue(validate(profile, self.probes))

    def test_missing_source_and_reason_cannot_pass(self):
        profile = copy.deepcopy(self.profile)
        profile["samples"][0]["thermal_state"] = metric(None, "enum", "raw/probes.json#missing")
        self.assertTrue(validate(profile, self.probes))

    def test_inventory_cannot_claim_benchmark(self):
        profile = copy.deepcopy(self.profile)
        profile["certified"] = True
        self.assertTrue(validate(profile, self.probes))


if __name__ == "__main__":
    unittest.main()
