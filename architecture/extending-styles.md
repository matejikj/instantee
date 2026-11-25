# Extending a style without GPU handles

V1 ships three Style Bible packages: `editorial`, `scientific-notebook` and
`paper-cinema`. Styles are **data and direction configuration**, not Metal
backend forks (ADR-0004). You can change tokens, assets and authoring tags
without opening GPU device, texture or command-buffer types.

## What to edit

| Layer | Location | Typical change |
| --- | --- | --- |
| Style Bible tokens | `contracts/fixtures/style-bible/positive/<style>.style.json` | colors, fonts, motion, camera, `tokens.safe_area` |
| Style assets + lock | `assets/<style>/` (`*.lock.json`, `PIN.json`, textures, fonts) | new texture/font with license sidecar |
| Authoring tag | `authoring/packages/authoring/src/policy.ts` (`STYLE_PACKAGES`) | register `name@version` for the SDK |
| Direction / lint | `engine/src/direction/` | constraints, lint codes, package load — still no GPU |
| Compiler lowering | `engine/src/compiler/` | map directed film → Scene IR |
| Visual baseline | `contracts/visual-baseline/` + style goldens | only when pixels intentionally change |

Verified public example: `authoring/examples/three-styles/main.mjs` imports only
`@instantee/authoring` and the locked style tags.

## What not to touch for a style change

- `engine/src/render-metal/` and `third_party/metal-cpp/`
- `MTL*`, `id<MTL*>`, IOSurface or CVPixelBuffer types
- `engine/native/export-apple/` encoder internals
- Shader MSL unless a **new render-core feature** is required (that is a render
  feature task, not a style-token task)

If a style needs a new draw capability, add a GPU-neutral feature in
`render-core` first, then teach `render-metal` to implement it. Style packages
select features by data; they do not embed backend handles.

## Checklist

1. Copy or edit a Style Bible fixture; keep `package_id` / semver coherent.
2. Ensure `tokens.safe_area` insets remain usable (quality-release gate).
3. Update asset lock digests offline; do not fetch fonts/textures at build.
4. Expose the tag on the authoring SDK if authors should select it.
5. Run direction/compiler tests and, for pixel-affecting changes, the visual
   baseline / quality lint gates.
6. Confirm `tools/dev/check_module_boundaries.py` still passes — direction must
   not gain a `render-metal` edge.
