# B-02 notebook Pythagoras explainer

Locked 60 s / 1800-frame Scene IR for the scientific-notebook explainer demo.
Hashes in `b02-workload.lock.json` pin scene, asset lock, style, fonts, quality
tier and encoder profile before the first reference-series measurement.

Layouts follow the notebook composition templates: problem statement,
construction, derivation, visual intuition and summary.

## Visual acceptance
Shot-boundary structural dumps and PNG reference frames live under `goldens/`.
Updates require an entry in `goldens/CHANGE_RECORD.json`.
Checklist, tolerances, and the signed review record are under `visual-review/`.
Coverage includes math text, ink geometry, note/paper image layers, opacity
interpolation, and phone (1080×1920) legibility. Subjective impression alone
is not acceptance.
