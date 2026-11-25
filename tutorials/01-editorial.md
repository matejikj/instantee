# Tutorial: Editorial

Creates a one-second Editorial news hook and exports `editorial.mp4`.

## Prerequisites

Complete the [clean install](README.md#clean-install-once).

## Steps

```sh
WORK=/tmp/instantee-tutorials
mkdir -p "$WORK"

node authoring/examples/tutorials/editorial/main.mjs \
  --out "$WORK/editorial.authoring.json"

build/release/instantee compile "$WORK/editorial.authoring.json" \
  --out "$WORK/editorial.scene.json" \
  --style-bible contracts/fixtures/style-bible/positive/editorial.style.json

build/release/instantee render "$WORK/editorial.scene.json" \
  --out "$WORK/editorial.mp4" \
  --overwrite
```

Style assets for richer films: `assets/editorial/` (see
`assets/editorial/README.md` and `SYNTHETIC_MEDIA_LICENSE.txt`).

## Check

```sh
test -s "$WORK/editorial.mp4"
```
