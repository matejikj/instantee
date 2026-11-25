# Evidence: macOS release packaging

## Acceptance

A relocatable macOS arm64 CLI package installs and renders without compiling the
Metal renderer on the host. Metallibs are embedded; the binary links only Apple
system libraries; INSTALL_RPATH is `@loader_path/../lib`; signing is ad-hoc.

## Measurement

`none` — no performance claims or certified reference-series runs.

## Evidence tier

A — commands, tests, install smoke, and `checks.json`.

## Artifacts

- `package-policy.json` — anonymized POLICY excerpt (relative paths only)
- `install-smoke.json` — smoke result (`invoked_cmake=false`)
- `commands.txt` — reproduction commands
- `checks.json` — `tools/dev/check.py` report
