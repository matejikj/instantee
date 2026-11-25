# Decision: P8 phase gate / V1.0.0 release

## Decision

**PASS** — create local annotated tag `v1.0.0` on the release commit that
carries this evidence package.

## Verified

- All P8 tasks are done with evidence after this gate closes.
- Prior phase gates P0–P7 remain PASS/GO.
- V1 certification dossier stays accepted.
- Version manifest axes stay consistent with CMake, npm, Style Bibles, and CHANGELOG.
- macOS arm64 package `instantee-macos-1.0.0-arm64` smoke-installs without compiling Metal.
- Full `tools/dev/check.py --gate` matrix passed for this delivery tree (`result: passed`).

## Non-claims

- Remotes are not pushed; the tag is local until an operator publishes it.
- Package digests are not Apple notarization.
- No new certified M1 Air performance series is claimed here.
