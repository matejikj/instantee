#!/usr/bin/env python3
"""Check inventory integrity and corroborate a saved profile against the current Mac."""

import argparse
import json
from pathlib import Path
import re

from collect import Probes, integer, reference_identity, sha256, utc_now


def validate(profile, probes):
    errors = []

    def walk(value, path="profile"):
        if isinstance(value, dict):
            if {"value", "unit", "source", "reason"} <= value.keys():
                if not value["unit"] or not value["source"]:
                    errors.append(path + ": missing unit/source")
                if value["value"] is None and not value["reason"]:
                    errors.append(path + ": unavailable value without reason")
                if value["source"].startswith("raw/probes.json#"):
                    key = value["source"].split("#", 1)[1]
                    if key not in probes:
                        errors.append(path + ": missing source record")
            for key, item in value.items():
                walk(item, f"{path}.{key}")
        elif isinstance(value, list):
            for index, item in enumerate(value):
                walk(item, f"{path}[{index}]")

    walk(profile)
    hw = profile["hardware"]
    identity = reference_identity(*(hw[key]["value"] for key in
                                  ("model", "soc", "ram", "gpu_cores", "physical_cpu", "logical_cpu")))
    if identity != profile["reference"] or identity["status"] != "matched":
        errors.append("Reference identity is mismatched, missing or incorrectly classified")
    for key in ("model", "soc", "ram", "gpu_cores", "physical_cpu", "logical_cpu",
                "performance_cpu", "efficiency_cpu", "model_sysctl"):
        if hw[key]["value"] is None:
            errors.append("Missing hardware metric: " + key)
    if hw["model"]["value"] != hw["model_sysctl"]["value"]:
        errors.append("system_profiler and sysctl disagree on model")
    for key in ("build", "version", "architecture"):
        if profile["os"][key]["value"] is None:
            errors.append("Missing OS metric: " + key)
    for key in ("selected_developer_directory", "clang", "sdk_version", "sdk_build", "sdk_path"):
        if profile["toolchain"][key]["value"] is None:
            errors.append("Missing selected toolchain metric: " + key)
    for disk in profile["storage"].values():
        if any(disk[key]["value"] is None for key in ("device", "filesystem", "internal", "available")):
            errors.append("Incomplete storage identity")
        if not disk["physical_disks"] or any(item["value"] is None for item in disk["physical_disks"]):
            errors.append("Missing physical storage mapping")
    samples = profile["samples"]
    if len(samples) != profile["sampling"]["count"] or len(samples) < 2:
        errors.append("Incomplete sample sequence")
    for sample in samples:
        for key in ("thermal_state", "low_power_mode", "physical_memory_bytes", "logical_cpu_count", "power", "swap", "load_average"):
            if sample[key]["value"] is None:
                errors.append("Missing sample: " + key)
        if sample["physical_memory_bytes"]["value"] != hw["ram"]["value"]:
            errors.append("Foundation and sysctl disagree on RAM bytes")
        if sample["logical_cpu_count"]["value"] != hw["logical_cpu"]["value"]:
            errors.append("Foundation and sysctl disagree on logical CPUs")
    if any(b["elapsed_ns"] <= a["elapsed_ns"] for a, b in zip(samples, samples[1:])):
        errors.append("Non-monotonic samples")
    if profile["certified"] or profile["benchmark"]["performed"]:
        errors.append("Inventory must not assert benchmark certification")
    return errors


def verify(directory, output):
    profile = json.loads((directory / "machine-profile.json").read_text())
    raw = json.loads((directory / "raw/probes.json").read_text())
    errors = validate(profile, raw)
    for name, digest in json.loads((directory / "SHA256.json").read_text()).items():
        if sha256(directory / name) != digest:
            errors.append("Evidence digest differs: " + name)
    for name, digest in profile["collector"]["sources_sha256"].items():
        if sha256(Path(__file__).parent / name) != digest:
            errors.append("Collector source differs: " + name)
    live = Probes()
    checks = []

    def compare(name, expected, actual):
        passed = expected is not None and actual is not None and expected == actual
        checks.append({"name": name, "expected": expected, "actual": actual, "passed": passed})
        if not passed:
            errors.append("Live comparison failed: " + name)

    for field, key in (("model", "hw.model"), ("ram", "hw.memsize"), ("physical_cpu", "hw.physicalcpu"),
                       ("logical_cpu", "hw.logicalcpu"), ("performance_cpu", "hw.perflevel0.physicalcpu"),
                       ("efficiency_cpu", "hw.perflevel1.physicalcpu")):
        actual = live.run(key, ["/usr/sbin/sysctl", "-n", key])
        compare(key, profile["hardware"][field]["value"], actual if field == "model" else integer(actual))
    # A separate text output path corroborates the JSON GPU core count.
    display = live.run("gpu", ["/usr/sbin/system_profiler", "SPDisplaysDataType"])
    cores = re.search(r"Total Number of Cores:\s*(\d+)", display or "")
    soc = re.search(r"Chipset Model:\s*(.+)", display or "")
    compare("GPU cores", profile["hardware"]["gpu_cores"]["value"], int(cores[1]) if cores else None)
    compare("GPU SoC", profile["hardware"]["soc"]["value"], soc[1].strip() if soc else None)
    for field, flag in (("build", "-buildVersion"), ("version", "-productVersion")):
        compare("macOS " + field, profile["os"][field]["value"], live.run(field, ["/usr/bin/sw_vers", flag]))
    for name, disk in profile["storage"].items():
        df = live.run("df_" + name, ["/bin/df", "-P", disk["path"]])
        device = df.splitlines()[-1].split()[0].removeprefix("/dev/") if df else None
        compare("df device " + name, disk["device"]["value"], device)
    compare("Selected SDK version", profile["toolchain"]["sdk_version"]["value"],
            live.run("sdk_version", ["/usr/bin/xcrun", "--sdk", "macosx", "--show-sdk-version"]))
    result = {"verified_at_utc": utc_now(), "status": "pass" if not errors else "blocked",
              "profile_sha256": sha256(directory / "machine-profile.json"),
              "verifier_sha256": sha256(__file__), "errors": errors, "live_checks": checks,
              "live_probes": live.records,
              "scope": "Inventory acceptance only; dynamic power/load values may change; no benchmark certification"}
    with output.open("x") as stream:
        stream.write(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile_directory", type=Path)
    parser.add_argument("--output", required=True, type=Path, help="New verification JSON file")
    args = parser.parse_args()
    result = verify(args.profile_directory, args.output)
    print(json.dumps({"status": result["status"], "errors": result["errors"]}))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
