# Deterministic release asset lock

`contracts/release/release-asset-lock-v1.json` pins repository-relative fonts,
textures, paper-prop blueprint goldens, Style Bible fixtures, nested style
locks, licenses and certification build flags. Paths use a repo-relative
sandbox (not limited to `assets/`).

`verify_release_asset_lock` hashes files offline, checks optional license
sidecars, and re-finalizes the content digest. Changing an entry hash changes
`content_sha256`. The reproducibility suite also reloads a locked Scene IR
fixture twice and compares canonical digests.

Measurement: none. Evidence tier A (ASan selected by native change risk).
