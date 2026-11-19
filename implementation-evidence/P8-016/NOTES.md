# MP4 media conformance validation

Adds `validate_mp4_conformance` over independent AVFoundation inspection and
timeline decode. Checks H.264, 1080×1920, silence, Rec.709 tags, playability,
nominal rate, frame count, and exact FrameClock PTS.

Reference clips exported in-suite must pass; missing/garbage/truncated files and
mismatched expectations fail with stable error codes. Render-to-MP4 smoke uses
the same validator.

Measurement: none. Evidence tier B (native + ASan). Timing risk includes PTS.
