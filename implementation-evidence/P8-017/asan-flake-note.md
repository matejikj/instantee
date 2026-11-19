# Unrelated ASan flake observed during verification

First `check.py --asan` run failed solely on `spike_runtime_frame_eval_scheduler`
(`FAIL: close drained five frames`). Release CTest (341) passed, including the
color accuracy suite. That scheduler test is outside the color path; previously
reproduced as intermittent under ASan. Re-run of the full asan-ubsan matrix is
recorded in checks.json.
