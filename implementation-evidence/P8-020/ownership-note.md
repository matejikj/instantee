# Ownership / lifetime

Workload lock audit is pure Python over repo-relative paths; no threads or
native callbacks. Complexity tier aliases (`mixed-b04-v1`,
`worst-accepted-b05-v1`) are value-typed lookups with no heap ownership beyond
returned `SceneComplexityTier` copies.
