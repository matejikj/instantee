# Ownership / lifetime note

- Comparison helper is pure value inspection of caller-owned RGBA/BGRA buffers.
- Encode/decode paths use temporary VideoWriter pools and AVAssetReader frames
  released before the next sample; no shared mutable state across threads.
- Suite temp directories are removed after assertions; no published targets.
