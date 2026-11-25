# Security and license audit

Offline gate for distributed assets, vendored pins and the Node/vcpkg supply
chain. Produces an SBOM and an audit report; unresolved critical findings fail
the check.

## Inputs

- `allowed-licenses-v1.json` — SPDX-style allowlist for pins, assets and tooling
- `platform-limits-v1.json` — documented V1 platform and supply-chain limits
- `third_party/*/PIN.json` — archive identity and declared licenses
- `contracts/release/release-asset-lock-v1.json` — fonts, textures, licenses
- `vcpkg.json` / `package-lock.json` — lock manifests (via dependency audit)

## Local check

```sh
python3 tools/dev/check_security_audit.py \
  --write-report /tmp/security-audit-report.json \
  --write-sbom /tmp/sbom.json
```

Hosted CI runs the same gate under `security-audit` and uploads anonymized
artifacts. This is not a performance certification claim and does not download
advisories from the network; critical status is derived from lock/license/hash
policy findings.
