# SVG / bitmap / font import security fuzz

Bounded corpus driver for external asset importers. Inputs are capped before
decode; SVG security pre-scan rejects scripts, remote/file URLs, and complex
host elements. Bitmap probe/decode enforce pixel budgets. Fonts open only from
harness-owned temp files under a disposable sandbox directory.

## Ownership

Temp font files are written under `temp/instantee-fuzz-asset-import` and removed
after each open attempt / process exit. No network I/O is performed by the
importers under test.

Measurement: none. Evidence tier A (+ ASan on native driver).
