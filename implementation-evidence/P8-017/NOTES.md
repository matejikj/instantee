# Color accuracy roundtrip suite

Compares direct Rec.709/sRGB-transfer preview buffers to independently decoded
H.264 frames. Patterns cover brightness (mid gray), alpha flatten/overlay,
luminance gradient, fine text-like stems, and a scene preview with geometry +
text proxies. Defined channel tolerance is `kRec709RoundtripMaxChannelDelta`
(16). Strict zero-delta path fails closed on saturated encode/decode.

Measurement: none. Evidence tier B (native + ASan). Timing risk includes PTS.
Authoring/contracts suites: see authoring-checks.json (snapshot refresh).
Unrelated ASan flake in frame_eval_scheduler: see asan-flake-note.md; matrix re-run passed.
