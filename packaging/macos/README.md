# macOS release packaging

Relocatable binary distribution of the Instantee CLI and supporting payloads so
a clean supported Mac can install and render **without compiling the Metal
renderer**.

## Layout

```text
instantee-macos-<version>-arm64/
  bin/instantee
  lib/                         # reserved (empty in V1)
  share/instantee/
    contracts/…                # schemas + Style Bible fixtures
    fonts/…                    # pinned TTFs + license sidecars
    authoring/…                # @instantee/authoring package surface
    examples/tutorials/…       # public SDK tutorial generators
    smoke/smoke.scene.json     # precompiled Scene IR for install smoke
  POLICY.json                  # deployment target, RPATH, signing policy
  MANIFEST.json                # relative paths + SHA-256
  README.md
```

Metallibs are **embedded** in the CLI (offline headers). The binary links only
Apple system frameworks + `libc++`; see `policy-v1.json`.

## Build the package

```sh
cmake --preset release
cmake --build --preset release --target instantee
npm run build:authoring
python3 tools/release/package_macos.py \
  --build-dir build/release \
  --out-dir build/dist/macos \
  --version 1.0.0
```

## Install smoke (no renderer compile)

```sh
python3 tools/release/smoke_install_macos.py \
  --package-dir build/dist/macos/instantee-macos-*-arm64 \
  --work-dir /tmp/instantee-install-smoke
```

The smoke copies the package to a temporary prefix, runs `instantee help` and
renders the bundled smoke Scene IR to MP4. It never invokes `cmake` or Metal
shader compilation.
