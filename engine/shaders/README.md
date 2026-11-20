# Offline Metal shaders

Pinned `.metal` sources live under `spike/` and are compiled into metallibs by
`cmake/CompileMetallib.cmake` during the native build. Module identity and
content hashes are locked in `modules/PIN.json`.

## Changing a shader

1. Edit the `.metal` source.
2. Refresh the PIN hash:
   `python3 tools/dev/check_shader_pin.py --write`
3. If a constant buffer or instance stride changed, update the matching
   `static_assert(sizeof(...))` and `contracts/shaders/cpu-gpu-layouts-v1.json`.
4. Re-run Metal goldens (`ctest -L golden`) and refresh expected PNG hashes only
   with an intentional review.
5. Local gate: `python3 tools/dev/run_gpu_integration.py -p build/dev --artifact-dir /tmp/gpu-artifacts`

Hosted CI job `gpu-integration` blocks the pull request when the PIN, layout
contract or golden/retirement/video-stress suite fails. Durations from that job
are correctness diagnostics, not certified release timings.
