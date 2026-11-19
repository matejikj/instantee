# Unrelated ASan flake observed during verification

During the first `check.py --asan` run, `spike_runtime_frame_eval_scheduler`
failed once under the asan-ubsan preset with:

`FAIL: close drained five frames`

That test does not exercise scene complexity measurement. A 20-iteration
stress of the same ASan binary reproduced the intermittent assertion
(~1/20). The complexity unit test binary under ASan exits 0. Release
`ctest` (330 tests) passed on the first run, including the scheduler test.

This is recorded as pre-existing concurrency flake debt outside this
delivery; do not treat it as a complexity-budget regression.
