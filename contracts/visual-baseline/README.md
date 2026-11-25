# Visual regression baseline

The three Style Bible workloads keep locked PNG and structural goldens under
`contracts/fixtures/b0{1,2,3}-*/goldens/`. This registry binds those goldens to
the Apple Silicon macOS Metal family, shared pixel tolerances, and an explicit
approval workflow.

## Release-gate rules

- Manifest digests must match bytes on disk.
- Every current digest must be covered by `CHANGE_RECORD.json`.
- `visual-review/review-record.json` must be `approved` and hash-bind the
  golden manifest.
- Readability checklist items listed in `registry-v1.json` must pass.
- Hosted CI job `visual-regression` fails the pull request on unreviewed pixel
  drift or failed readability items.

## Updating a golden

1. Reproduce the style visual-acceptance test and inspect the diff.
2. Replace the golden artifact intentionally.
3. Refresh `manifest.json` digests.
4. Append a `CHANGE_RECORD.json` entry with reason, role, and UTC time.
5. Update `visual-review/review-record.json` (approved status, new manifest
   hash, checklist results).
6. Run:

```sh
python3 tools/dev/check_visual_baseline.py --enforce-platform
python3 tools/dev/run_visual_regression.py -p build/dev --artifact-dir /tmp/visual-artifacts
```

Subjective impression alone is never enough. Tolerances live in each style's
`visual-review/tolerances.json` and must match the registry defaults unless a
reviewed exception is documented in the change record reason.
