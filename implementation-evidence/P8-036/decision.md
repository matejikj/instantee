# Decision: V1 release certification dossier

## Decision

**Accept** the V1 release certification dossier.

All required checklist items pass against committed evidence: reference films,
B-01..B-05 threshold/endurance gates, video conformance, security/licenses,
tutorials/docs, FR/NFR coverage, and the ownership / concurrency / crash /
measurement audits.

## Explicit non-claims

- Git tag `v1.0.0` is **not** created by this delivery.
- Individual run reports remain `certified=false`.
- This dossier does not re-measure performance; it aggregates prior evidence.
- The P8 phase gate closes only after the verified tag step.

## Result

`product_v1_certificate=true` for the dossier package. Release tagging remains
the next delivery.
