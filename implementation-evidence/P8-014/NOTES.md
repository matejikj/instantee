# Asset URI sandbox path-boundary audit

Audits the P1 URI sandbox: lexical syntax gate plus symlink-aware resolve.
Canonicalization uses `weakly_canonical`; containment is decided with
`lexically_relative` so string-prefix tricks cannot hide escapes.

Symlinks under `assets/` that resolve outside the project root or outside
`assets/` are rejected. Policy ownership remains in `scene_ir` — loaders opt in
with `LoadOptions::enforce_uri_sandbox`.

Measurement: none. Evidence tier B (filesystem / path security).
