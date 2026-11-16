# Reproducible machine profile

The collector uses Python 3 with the standard library, macOS system commands
and a small Objective-C++ Foundation helper built through the selected Xcode
using `clang++ -std=c++23`. It installs no tools and changes neither power
settings nor system caches. The helper is compiled before sampling; its binary
is removed with the private temporary directory. It is not the framework
runtime or a libc++ feature test.

From the repository root:

```sh
python3 tools/machine-profile/collect.py \
  --output /tmp/instantee-device-profile \
  --storage-path "$PWD" --samples 5 --interval-seconds 1
python3 tools/machine-profile/verify.py /tmp/instantee-device-profile \
  --output /tmp/instantee-device-verification.json
python3 -m unittest discover -s tools/machine-profile -p 'test_*.py' -v
```

Choose a new output for each collection and verification. Existing files are
never overwritten. `--storage-path` must be an existing directory on the future
output volume; APFS is queried using its actual mount point, not an arbitrary
subdirectory. Collection returns 0 for a matching reference hardware identity,
2 for a different/unverified machine, and 1 for collection failure. Exit code 0
does not establish profile completeness: running the verifier is mandatory.
It returns 0 only for a complete profile, matching digests and successful
comparison with the current machine. Insufficient permissions cause failure;
identity is never guessed from a model name or marketed RAM capacity.

`raw/probes.json` stores argv, UTC start time, query duration in ns, return code,
stderr and command output. For `system_profiler` and `diskutil`, fields are
explicitly allowlisted: serial numbers, UUIDs, display EDID and user names are
not stored. Disk names such as `diskN` identify the current boot, not a permanent
device. The hardware configuration also records media model, capacity in bytes,
bus, internal/external location and APFS physical-store mapping. The profile
does not distinguish physically separate devices with identical configurations.

Each metric has `value`, `unit`, `source` and `reason`. `null` means unavailable
with a reason; an actual measured zero remains zero. RAM uses direct sysctl
bytes checked against Foundation `physicalMemory`, rather than converting a
marketed capacity string. Swap converts binary K/M/G units to bytes; precision
is limited by rounding in the original sysctl output. Load average is runnable
jobs over 1/5/15-minute windows. CPU percentages and system memory remain in
aggregated `top` output with units; the second sample covers a one-second
interval. Neither `top` nor the collector measures a future renderer process's
CPU usage.

Thermal state and low-power mode come from `NSProcessInfo` as defined in the
local `Foundation.framework/Headers/NSProcessInfo.h`. Unknown future enum values
become `null`. `pmset -g therm` is supplementary diagnostic data only: absence
of a historical warning does not prove `nominal`. Samples use UTC and monotonic
offsets in ns. Actual intervals are recorded; separate metric queries are not
atomic. A short series describes development conditions, not a thermal/endurance
test.

Xcode discovery checks `/Applications/Xcode*.app`, the active developer
directory, an optional `DEVELOPER_DIR` and `/Library/Developer/CommandLineTools`.
SDK inventory uses actual `SDKSettings.json` files; aliases are deduplicated by
resolved path. Active SDK version and build are also obtained through `xcrun`.
Standalone CLT has no Xcode version: that field is `null` with a reason, while
its package receipt and SDK remain present. Archived installations outside
these locations are not searched. Versions are not pinned by this collector.

Tests cover missing metrics, denied queries, timeouts, unit conversions, distinct
GPU variants and preservation of existing evidence without performing live
system measurements. Tests against saved measured evidence use only the ignored
local reference profile. Those tests are explicitly skipped when that profile
is absent, so a clean clone does not need private local evidence. Keep collected
profiles in ignored local storage; do not commit them.
