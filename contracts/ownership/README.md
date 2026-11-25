# Ownership inventory

`resource-inventory-v1.json` lists exclusive runtime resources that must declare:

- **owner** — who destroys / recycles the resource
- **borrow** — what views/handles may observe it and for how long
- **completion** — GPU/sink/publish point that allows recycle
- **limit** — capacity / budget that must not be bypassed
- **stop_or_error** — cancel/fault behaviour without UAF or double-release

Validate:

```sh
python3 tools/dev/check_ownership_audit.py
ctest --preset release -R ownership_lifetime_audit
```

Normative prose: `architecture/ownership-and-jobs.md` and
`motion_framework_sas/CPP_RUNTIME_CONTRACTS.md`.
