# Ownership / lifetime note (metric samples)

## Sample buffers

`FrameSamples` owns a bounded capacity. Overflow, reordered timestamps, or
non-monotonic request/sink times increment `lost_samples` and invalidate
quantile summaries. Missing metrics stay null; null is never coerced to zero.

## Report eligibility

Only `evidence_kind=measured` with `result=success`, `certified=false`, zero
lost samples, and semantically consistent formulas may be certification-
eligible. Schema fixtures remain structural examples only.

## Series reproduction

Independent reproduction owns only the committed baseline JSON rows. It does
not spawn renderer processes and does not rewrite run artifacts.
