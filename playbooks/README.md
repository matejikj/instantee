# Operational playbooks

Versioned runbooks for common failures and for performance measurement.
They complement the architecture guides and do **not** certify M1 Air
throughput, p99, CPU or memory by themselves.

| Playbook | Purpose |
| --- | --- |
| [Troubleshooting](troubleshooting.md) | Reproduce and diagnose export, font, color, GPU, memory and related failures |
| [Benchmark](benchmark.md) | Run measurement harnesses without confusing smoke runs for certification |

Dry-run coverage of the troubleshooting case structure:

```sh
python3 tools/dev/check_playbooks.py
```
