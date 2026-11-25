# Security and license audit

## Acceptance

Všechny distribuované assety mají oprávnění a žádná neřešená kritická zranitelnost.

## Decisions

- Offline gate over vendored `PIN.json` identity, metal-cpp tag/commit/archive
  hash (matching CMake configure gates), font content hashes, release asset
  lock digests and Node/vcpkg lock policy.
- Allowed licenses live in `contracts/security-audit/allowed-licenses-v1.json`;
  known platform/supply-chain limits in `platform-limits-v1.json`.
- Emit a CycloneDX 1.5-inspired SBOM plus audit report. No network CVE fetch;
  critical findings are lock/license/hash policy failures. Reports set
  `certified_performance: false`.

## Verification

- `python3 tools/dev/check_security_audit.py --write-report implementation-evidence/P8-029/security-audit-artifacts/security-audit-report.json --write-sbom implementation-evidence/P8-029/security-audit-artifacts/sbom.json`
  result: `passed` (critical=0).
- `python3 -m unittest discover -s tools/dev -p 'test_security_audit.py' -v`
  result: `passed`.
- `tools/dev/check.py --keep-going --report implementation-evidence/P8-029/checks.json`
  (see checks.json).
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`, anonymized SBOM and audit report).
