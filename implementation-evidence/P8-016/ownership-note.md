# Ownership / lifetime note

- Conformance validation opens media read-only through AVFoundation helpers;
  it does not own writers, pixel pools, or staging transactions.
- Timeline decode allocates temporary sample buffers released per frame before
  the next copy; no shared mutable state across threads.
- Callers retain path lifetime for the duration of `validate_mp4_conformance`.
- Negative fixtures (garbage/truncated) are local temp files cleaned by the suite.
