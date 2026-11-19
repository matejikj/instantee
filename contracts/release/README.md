# Release asset lock

`release-asset-lock-v1.json` is the V1 repository-wide reproducibility lock.

It records:

- pinned fonts and their license sidecars
- Style Bible fixtures for the three V1 styles
- nested per-style asset locks under `assets/`
- representative textures and paper-prop blueprint goldens
- synthetic-media license texts
- certification build flags (`cxx_standard`, CMake preset, sanitizer default)

Verification is offline-only (`verify_release_asset_lock`). Changing any locked
file bytes or an entry digest invalidates the lock. Regenerate the content
digest with:

```sh
cmake --build --preset release --target spike_scene_ir_release_lock_test
./build/release/engine/tests/unit/scene-ir/spike_scene_ir_release_lock_test --write-lock
```
