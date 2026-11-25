# End-to-end tutorials

Produce three short silent MP4s — one per V1 style — from a clean checkout
using only the public authoring SDK and the `instantee` CLI.

API reference: [`authoring/API.md`](../authoring/API.md) and
[`authoring/packages/authoring/PUBLIC_API.md`](../authoring/packages/authoring/PUBLIC_API.md).

| Tutorial | Style | Generator | Style Bible |
| --- | --- | --- | --- |
| [Editorial](01-editorial.md) | `editorial@1` | `authoring/examples/tutorials/editorial/main.mjs` | `contracts/fixtures/style-bible/positive/editorial.style.json` |
| [Notebook](02-notebook.md) | `scientific-notebook@1` | `authoring/examples/tutorials/notebook/main.mjs` | `contracts/fixtures/style-bible/positive/scientific-notebook.style.json` |
| [Paper Cinema](03-paper-cinema.md) | `paper-cinema@1` | `authoring/examples/tutorials/paper-cinema/main.mjs` | `contracts/fixtures/style-bible/positive/paper-cinema.style.json` |

Pinned demo assets (fonts/textures/licenses) live under `assets/<style>/` and
are verified offline by the release asset lock. These tutorial films are
text-led; they do not require copying assets into the work directory.

## Clean install (once)

From the repository root on an Apple Silicon Mac with the certified toolchain:

```sh
npm ci --ignore-scripts --no-audit --no-fund
npm run build:authoring
cmake --preset release
cmake --build --preset release --target instantee
```

The CLI binary is `build/release/instantee` (or `build/release/instantee` after
the preset places it there — confirm with `ls build/release/instantee`).

## Produce all three videos

```sh
mkdir -p /tmp/instantee-tutorials
python3 tools/dev/run_tutorial_e2e.py \
  --cli build/release/instantee \
  --work-dir /tmp/instantee-tutorials \
  --frames 30
```

Expected outputs:

- `/tmp/instantee-tutorials/editorial.mp4`
- `/tmp/instantee-tutorials/notebook.mp4`
- `/tmp/instantee-tutorials/paper-cinema.mp4`

`--frames` caps export length for smoke runs. Omit it (or set it ≥ shot length)
to encode the full tutorial shot (30 frames / 1 s at 30 fps in the samples).

This path is **not** a certified M1 Air performance measurement.
