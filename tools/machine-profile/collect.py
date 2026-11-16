#!/usr/bin/env python3
"""Collect a source-attributed macOS device snapshot without changing system settings."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import subprocess
import tempfile
import time


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def metric(value, unit, source, reason=None):
    return {"value": value, "unit": unit, "source": source,
            "reason": reason if value is None else None}


def integer(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def parse_power(text):
    source = re.search(r"Now drawing from '([^']+)'", text)
    battery = re.search(r"(\d+)%;\s*([^;]+);", text)
    if not source or not battery:
        raise ValueError("Power source or battery charge not reported")
    return {"source": source[1], "battery_charge_percent": int(battery[1]), "battery_state": battery[2]}


def parse_load(text):
    values = re.findall(r"\d+(?:\.\d+)?", text)
    if len(values) != 3:
        raise ValueError("Expected 1/5/15 minute load averages")
    return dict(zip(("1_minute", "5_minutes", "15_minutes"), map(float, values)))


def parse_swap(text):
    values = re.findall(r"(total|used|free)\s*=\s*(\d+(?:\.\d+)?)([KMG])", text)
    if {name for name, _, _ in values} != {"total", "used", "free"}:
        raise ValueError("Swap units or fields unrecognized; retain original source")
    return {name: round(float(value) * 1024**("KMG".index(unit) + 1)) for name, value, unit in values}


def reference_identity(model, soc, ram, gpu, physical, logical):
    expected = {"model": (model, "MacBookAir10,1"), "soc": (soc, "Apple M1"),
                "ram_bytes": (ram, 16 * 1024**3), "physical_cpu_cores": (physical, 8),
                "logical_cpu_cores": (logical, 8)}
    missing = [name for name, (actual, _) in expected.items() if actual is None]
    mismatches = [name for name, (actual, wanted) in expected.items()
                  if actual is not None and actual != wanted]
    if gpu is None:
        missing.append("gpu_cores")
    elif gpu not in (7, 8):
        mismatches.append("gpu_cores")
    status = "mismatch" if mismatches else "unverified" if missing else "matched"
    return {"status": status, "missing": missing, "mismatches": mismatches,
            "variant": f"m1-air-16gib-gpu{gpu}" if status == "matched" else None}


def mount_point(path):
    path = Path(path).resolve(strict=True)
    while not path.is_mount() and path.parent != path:
        path = path.parent
    return path


DISK_KEYS = (
    "DeviceIdentifier", "DeviceNode", "ParentWholeDisk", "MountPoint", "MediaName",
    "FilesystemType", "FilesystemName", "BusProtocol", "Internal", "SolidState",
    "TotalSize", "DeviceBlockSize", "VolumeAllocationBlockSize", "APFSContainerReference",
    "APFSContainerSize", "APFSContainerFree", "APFSPhysicalStores", "WritableVolume",
    "FileVault", "SMARTStatus", "Error", "ErrorMessage", "ExitCode",
)


def select_keys(value, keys):
    return {key: value[key] for key in keys if key in value}


def profiler_projection(value):
    return {
        "SPHardwareDataType": [select_keys(row, ("machine_model", "machine_name", "chip_type",
                                                    "number_processors", "physical_memory"))
                               for row in value.get("SPHardwareDataType", [])],
        "SPDisplaysDataType": [select_keys(row, ("sppci_model", "sppci_cores", "spdisplays_metal"))
                               for row in value.get("SPDisplaysDataType", [])],
    }


class Probes:
    def __init__(self):
        self.records = {}

    def run(self, key, argv, encoding="text", project=None, env=None):
        started = utc_now()
        before = time.monotonic_ns()
        record = {"argv": [str(a) for a in argv], "started_at_utc": started,
                  "environment_override": env or {}, "projection": project is not None}
        try:
            result = subprocess.run(record["argv"], capture_output=True, timeout=45,
                                    env={**os.environ, "LC_ALL": "C", **(env or {})})
            record.update(returncode=result.returncode, stderr=result.stderr.decode(errors="replace"))
            if encoding == "plist":
                output = plistlib.loads(result.stdout) if result.stdout else None
            elif encoding == "json":
                output = json.loads(result.stdout) if result.stdout else None
            else:
                output = result.stdout.decode(errors="replace").strip()
            record["output"] = project(output) if project and output is not None else output
        except (OSError, ValueError, plistlib.InvalidFileException, subprocess.TimeoutExpired) as error:
            record.update(returncode=None, output=None, stderr=str(error))
        record["elapsed_ns"] = time.monotonic_ns() - before
        self.records[key] = record
        return record["output"] if record["returncode"] == 0 else None

    def measure(self, key, argv, unit="text", convert=lambda x: x):
        value = self.run(key, argv)
        try:
            value = convert(value) if value is not None else None
        except (TypeError, ValueError):
            value = None
        return metric(value, unit, f"raw/probes.json#{key}",
                      self.records[key]["stderr"] or "Source missing or not parseable")


def inventory(probes):
    result = []
    developers = set(Path("/Applications").glob("Xcode*.app/Contents/Developer"))
    selected = probes.records["developer_directory"]["output"]
    for path in (selected, os.environ.get("DEVELOPER_DIR")):
        if path and Path(path).is_dir():
            developers.add(Path(path))
    clt = Path("/Library/Developer/CommandLineTools")
    if clt.exists():
        developers.add(clt)
    for index, developer in enumerate(sorted(developers)):
        prefix = f"toolchain_{index}"
        value = probes.run(prefix + "_version", ["/usr/bin/xcodebuild", "-version"],
                           env={"DEVELOPER_DIR": str(developer)})
        version = metric(value, "text", f"raw/probes.json#{prefix}_version",
                         "Xcode unavailable; standalone Command Line Tools are not Xcode")
        roots = [developer / "SDKs", developer / "Platforms/MacOSX.platform/Developer/SDKs"]
        sdks = []
        for sdk in sorted({path.resolve() for root in roots for path in root.glob("MacOSX*.sdk")}):
            settings = sdk / "SDKSettings.json"
            key = prefix + "_sdk_" + sdk.name
            metadata = None
            try:
                metadata = select_keys(json.loads(settings.read_text()),
                                       ("Version", "CanonicalName", "DisplayName", "ProductBuildVersion"))
                probes.records[key] = {"path": str(settings), "sha256": sha256(settings),
                                       "output": metadata, "collected_at_utc": utc_now()}
            except (OSError, ValueError) as error:
                probes.records[key] = {"path": str(settings), "output": None, "reason": str(error)}
            sdks.append({"path": str(sdk), "metadata": metric(metadata, "SDK metadata",
                         f"raw/probes.json#{key}", "SDKSettings.json unavailable")})
        result.append({"developer_directory": str(developer), "xcode_version": version, "macos_sdks": sdks})
    return result


def storage(probes, path, prefix):
    mount = mount_point(path)
    source = f"raw/probes.json#{prefix}"
    info = probes.run(prefix, ["/usr/sbin/diskutil", "info", "-plist", str(mount)],
                      "plist", lambda value: select_keys(value, DISK_KEYS)) or {}
    physical = []
    for index, store in enumerate(info.get("APFSPhysicalStores", [])):
        name = store.get("APFSPhysicalStore")
        if not name:
            continue
        key = f"{prefix}_physical_{index}"
        part = probes.run(key, ["/usr/sbin/diskutil", "info", "-plist", name],
                          "plist", lambda value: select_keys(value, DISK_KEYS)) or {}
        parent = part.get("ParentWholeDisk")
        if parent:
            key += "_whole"
            disk = probes.run(key, ["/usr/sbin/diskutil", "info", "-plist", parent],
                              "plist", lambda value: select_keys(value, DISK_KEYS))
            physical.append(metric(disk, "diskutil metadata; sizes in bytes",
                                   f"raw/probes.json#{key}", "Physical disk info unavailable"))
    stat = os.statvfs(path)
    probes.records[prefix + "_statvfs"] = {"path": str(path), "collected_at_utc": utc_now(),
        "f_frsize": stat.f_frsize, "f_blocks": stat.f_blocks, "f_bavail": stat.f_bavail}
    return {
        "path": str(Path(path).resolve()), "mount_point": str(mount),
        "device": metric(info.get("DeviceIdentifier"), "identifier", source, "diskutil unavailable"),
        "filesystem": metric(info.get("FilesystemType"), "name", source, "diskutil unavailable"),
        "internal": metric(info.get("Internal"), "boolean", source, "diskutil unavailable"),
        "container_size": metric(info.get("APFSContainerSize"), "bytes", source, "Not an APFS container or unavailable"),
        "available": metric(stat.f_frsize * stat.f_bavail, "bytes",
                            f"raw/probes.json#{prefix}_statvfs"),
        "volume_metadata": metric(info or None, "diskutil metadata; sizes in bytes", source, "diskutil unavailable"),
        "physical_disks": physical,
        "durability_class": metric(None, "class", "Not tested", "Inventory does not prove sync or power-loss guarantees"),
    }


def collect(output, storage_path, samples, interval):
    if platform.system() != "Darwin":
        raise ValueError("The collector requires macOS; no reference profile was created")
    # Each run owns a fresh directory so previous raw evidence cannot be overwritten.
    output.mkdir(parents=True, exist_ok=False)
    (output / "raw").mkdir()
    probes = Probes()
    started = utc_now()
    source_dir = Path(__file__).resolve().parent
    hardware = probes.run("system_profiler", ["/usr/sbin/system_profiler", "SPHardwareDataType",
                          "SPDisplaysDataType", "-json"], "json", profiler_projection) or {}
    hw_rows = hardware.get("SPHardwareDataType", [])
    gpu_rows = hardware.get("SPDisplaysDataType", [])
    hw = hw_rows[0] if len(hw_rows) == 1 else {}
    gpu = gpu_rows[0] if len(gpu_rows) == 1 else {}
    source = "raw/probes.json#system_profiler"
    machine = {name: metric(hw.get(key), "name", source, "Hardware field unavailable")
               for name, key in (("model", "machine_model"), ("soc", "chip_type"))}
    machine["gpu_cores"] = metric(integer(gpu.get("sppci_cores")), "cores", source,
                                   "A single GPU with an explicit core count was not reported")
    for name, key, unit in (("ram", "hw.memsize", "bytes"), ("physical_cpu", "hw.physicalcpu", "cores"),
                            ("logical_cpu", "hw.logicalcpu", "cores"),
                            ("performance_cpu", "hw.perflevel0.physicalcpu", "cores"),
                            ("efficiency_cpu", "hw.perflevel1.physicalcpu", "cores")):
        machine[name] = probes.measure(key, ["/usr/sbin/sysctl", "-n", key], unit, integer)
    machine["model_sysctl"] = probes.measure("hw.model", ["/usr/sbin/sysctl", "-n", "hw.model"], "name")
    os_profile = {key: probes.measure(key, ["/usr/bin/sw_vers", flag], "version") for key, flag in
                  (("product", "-productName"), ("version", "-productVersion"), ("build", "-buildVersion"))}
    os_profile["architecture"] = probes.measure("uname", ["/usr/bin/uname", "-m"], "architecture")
    toolchain = {"selected_developer_directory": probes.measure("developer_directory", ["/usr/bin/xcode-select", "-p"], "path"),
                 "clang": probes.measure("clang", ["/usr/bin/xcrun", "clang++", "--version"])}
    for key, flag in (("sdk_version", "--show-sdk-version"), ("sdk_build", "--show-sdk-build-version"),
                      ("sdk_path", "--show-sdk-path")):
        toolchain[key] = probes.measure(key, ["/usr/bin/xcrun", "--sdk", "macosx", flag])
    toolchain["installed"] = inventory(probes)
    toolchain["clt_receipt"] = probes.measure("clt_receipt", ["/usr/sbin/pkgutil", "--pkg-info",
                                                            "com.apple.pkg.CLTools_Executables"])
    toolchain["developer_dir_environment"] = metric(os.environ.get("DEVELOPER_DIR"), "path", "Environment",
                                                     "Unset; xcrun uses xcode-select selection")
    disks = {"workspace_and_candidate_output": storage(probes, storage_path, "workspace_disk"),
             "system": storage(probes, "/", "system_disk")}
    power_settings = probes.measure("power_settings", ["/usr/bin/pmset", "-g", "custom"])
    thermal_diagnostics = probes.measure("thermal_diagnostics", ["/usr/bin/pmset", "-g", "therm"])
    background = probes.measure("top", ["/usr/bin/top", "-l", "2", "-s", "1", "-n", "0"],
                                "top text; CPU %, memory with printed units; second sample interval 1 s")
    series = []
    with tempfile.TemporaryDirectory(prefix="instantee-profile-") as temporary:
        helper = Path(temporary) / "system-state"
        compiled = probes.run("helper_compile", ["/usr/bin/xcrun", "clang++", "-std=c++23", "-fobjc-arc",
                               "-Wall", "-Wextra", "-Werror", "-framework", "Foundation",
                               str(source_dir / "system_state.mm"), "-o", str(helper)])
        start_ns = time.monotonic_ns()
        for index in range(samples):
            deadline = start_ns + int(index * interval * 1e9)
            time.sleep(max(0, (deadline - time.monotonic_ns()) / 1e9))
            sample = {"at_utc": utc_now(), "elapsed_ns": time.monotonic_ns() - start_ns}
            key = f"state_{index}"
            state = probes.run(key, [str(helper)], "json") if compiled is not None else None
            for field, unit in (("thermal_state", "enum: nominal/fair/serious/critical"),
                                ("low_power_mode", "boolean"), ("physical_memory_bytes", "bytes"),
                                ("logical_cpu_count", "cores"), ("active_cpu_count", "cores")):
                sample[field] = metric((state or {}).get(field), unit, f"raw/probes.json#{key}",
                                       "Foundation helper unavailable or returned unknown value; see helper_compile/state probe")
            sample["power"] = probes.measure(f"battery_{index}", ["/usr/bin/pmset", "-g", "batt"],
                                               "source name, charge percent, state", parse_power)
            sample["load_average"] = probes.measure(f"load_{index}", ["/usr/sbin/sysctl", "-n", "vm.loadavg"],
                                                      "runnable jobs; 1/5/15 minute means", parse_load)
            sample["swap"] = probes.measure(f"swap_{index}", ["/usr/sbin/sysctl", "-n", "vm.swapusage"],
                                              "bytes; rounded from sysctl binary K/M/G units", parse_swap)
            series.append(sample)
    profile = {
        "schema_version": 1, "evidence_kind": "measured_machine_inventory", "certified": False,
        "started_at_utc": started, "finished_at_utc": utc_now(),
        "collector": {"python": platform.python_version(), "sources_sha256":
                      {file: sha256(source_dir / file) for file in ("collect.py", "system_state.mm")}},
        "hardware": machine, "os": os_profile, "toolchain": toolchain, "storage": disks,
        "power_settings": power_settings, "thermal_diagnostics": thermal_diagnostics,
        "background_load": background, "samples": series,
        "sampling": {"requested_interval_seconds": interval, "count": samples,
                     "actual_intervals_ns": [b["elapsed_ns"] - a["elapsed_ns"] for a, b in zip(series, series[1:])],
                     "scope": "Device inventory during development; commands and helper contribute load",
                     "thermal_before": series[0]["thermal_state"], "thermal_after": series[-1]["thermal_state"]},
        "reference": reference_identity(*(machine[key]["value"] for key in
                        ("model", "soc", "ram", "gpu_cores", "physical_cpu", "logical_cpu"))),
        "ambient_temperature": metric(None, "degrees Celsius", "No calibrated sensor", "Not measured"),
        "benchmark": {"performed": False, "conditions_document": "../README.md",
                      "power_policy": "AC; low power mode disabled; reject power changes",
                      "thermal_policy": "nominal before each run; minimum 5 minute cooldown and stable nominal for 60 s",
                      "cache_policy": "3 cold + 3 warm independent processes; app cache only; no OS purge",
                      "limits": metric(None, "workload-specific limits", "No benchmark executed",
                                       "p99/memory/CPU limits require measured baseline before certification")},
    }
    write_json(output / "raw/probes.json", probes.records)
    write_json(output / "machine-profile.json", profile)
    write_json(output / "SHA256.json", {name: sha256(output / name)
                                        for name in ("machine-profile.json", "raw/probes.json")})
    return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New directory; existing evidence is never overwritten")
    parser.add_argument("--storage-path", type=Path, default=Path.cwd(), help="Existing candidate output directory")
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--interval-seconds", type=float, default=1)
    args = parser.parse_args()
    if not 2 <= args.samples <= 600 or not 0.1 <= args.interval_seconds <= 60:
        parser.error("Require 2..600 samples and interval 0.1..60 seconds")
    try:
        profile = collect(args.output, args.storage_path, args.samples, args.interval_seconds)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Collection failed: {error}\n")
    print(json.dumps(profile["reference"]))
    return 0 if profile["reference"]["status"] == "matched" else 2


if __name__ == "__main__":
    raise SystemExit(main())
