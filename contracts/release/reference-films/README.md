# Publishable reference films

Three silent 60 s vertical Reels — one per V1 Style Bible — intended as the
publishable creative references for release:

| Film | Style | Scene IR |
| --- | --- | --- |
| `editorial-reference.mp4` | editorial@1.0.0 | `contracts/fixtures/b01-editorial/` |
| `notebook-reference.mp4` | scientific-notebook@1.0.0 | `contracts/fixtures/b02-notebook/` |
| `paper-cinema-reference.mp4` | paper-cinema@1.0.0 | `contracts/fixtures/b03-paper/` |

MP4 binaries are **not** stored in git. Export them locally (or in CI artifact
storage) and record digests in `catalog-v1.json`.

## Export

```sh
cmake --build --preset release --target instantee
python3 tools/release/export_reference_films.py \
  --cli build/release/instantee \
  --out-dir build/dist/reference-films \
  --write-digests
```

## Creative signoff

`creative-signoff-v1.json` records role-based approval of visual direction,
tempo, transitions, and phone text readability. It depends on the locked
per-style `visual-review/review-record.json` files and a technical MP4 probe.

## Verify

```sh
python3 tools/release/check_reference_films.py \
  --out-dir build/dist/reference-films \
  --require-files
```
