# Implementation architecture documentation

## Acceptance

Nový vývojář dokáže vyhledat owner modulu a rozšířit styl bez dotazu na vnitřní GPU handle.

## Decisions

- Keep `ARCHITECTURE.md` as the short hub; put module catalog, data flow,
  ownership/`RenderJob` phases, style-extension guide and ADR index under
  versioned `architecture/` (not the ignored local `/docs/` tree).
- Derive the module table from `cmake/module_graph.json` and enforce coverage
  with `tools/dev/check_architecture_docs.py` (also run from `dependency-policy`).
- Style extension documentation points at Style Bible / assets / authoring /
  direction and explicitly excludes metal-cpp and GPU handle APIs.

## Verification

- `python3 tools/dev/check_architecture_docs.py` result: `passed`.
- `python3 -m unittest discover -s tools/dev -p 'test_architecture_docs.py' -v`
  result: `passed`.
- `tools/dev/check.py --keep-going --report implementation-evidence/P8-030/checks.json`
  (see checks.json).
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`, architecture doc check transcript).
