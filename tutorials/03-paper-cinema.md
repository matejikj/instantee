# Tutorial: Paper Cinema

Creates a one-second Paper Cinema title card and exports `paper-cinema.mp4`.

## Prerequisites

Complete the [clean install](README.md#clean-install-once).

## Steps

```sh
WORK=/tmp/instantee-tutorials
mkdir -p "$WORK"

node authoring/examples/tutorials/paper-cinema/main.mjs \
  --out "$WORK/paper-cinema.authoring.json"

build/release/instantee compile "$WORK/paper-cinema.authoring.json" \
  --out "$WORK/paper-cinema.scene.json" \
  --style-bible contracts/fixtures/style-bible/positive/paper-cinema.style.json

build/release/instantee render "$WORK/paper-cinema.scene.json" \
  --out "$WORK/paper-cinema.mp4" \
  --overwrite
```

Style assets: `assets/paper-cinema/`.

## Check

```sh
test -s "$WORK/paper-cinema.mp4"
```
