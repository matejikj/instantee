# Benchmark workload catalog

`catalog-v1.json` lists the versioned B-01..B-05 workload locks used for
regression. Offline verification:

```sh
python3 tools/benchmark/verify_workload_locks.py --json
```

Policy: regression compares the same `workload_id` and pinned digests. Any
fixture or profile input change is an explicit new baseline (new `workload_id`
and updated catalog digest).
