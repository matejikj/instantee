# B-03 historical Paper Cinema reel

Locked 60 s / 1800-frame Scene IR for the Paper Cinema historical demo.
Hashes in `b03-workload.lock.json` pin scene, asset lock, style, font, quality
tier and encoder profile before the first reference-series measurement.

Shots follow the curated templates: reveal stage, object closeup, puppet
dialogue, technical explanation and atmospheric dissolve. The figure uses
the historical reference puppet pack provenance; fold and depth layers are
authored as Scene IR geometry/image nodes with framework animation tracks.

## Goldens and visual review
`goldens/` pins shot-boundary structural dumps and mid-shot PNG reference
frames. `visual-review/` holds the checklist, tolerances, and signed art
review record. Updates require an explicit `CHANGE_RECORD.json` entry;
subjective impression alone is not acceptance.

## Notes
Preview raster uses proxy fills for text/geometry/image/puppet. Export is
framework-native silent 1080×1920 H.264 only — no external video assembly.
