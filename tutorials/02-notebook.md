# Tutorial: Scientific Notebook

Creates a one-second Notebook explainer and exports `notebook.mp4`.

## Prerequisites

Complete the [clean install](README.md#clean-install-once).

## Steps

```sh
WORK=/tmp/instantee-tutorials
mkdir -p "$WORK"

node authoring/examples/tutorials/notebook/main.mjs \
  --out "$WORK/notebook.authoring.json"

build/release/instantee compile "$WORK/notebook.authoring.json" \
  --out "$WORK/notebook.scene.json" \
  --style-bible contracts/fixtures/style-bible/positive/scientific-notebook.style.json

build/release/instantee render "$WORK/notebook.scene.json" \
  --out "$WORK/notebook.mp4" \
  --overwrite
```

Style assets: `assets/notebook/`.

## Check

```sh
test -s "$WORK/notebook.mp4"
```
