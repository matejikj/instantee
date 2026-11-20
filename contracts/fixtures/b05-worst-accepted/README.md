# B-05 worst-accepted 300 s reel

Locked 300 s / 9000-frame stress Scene IR for the V1 worst-accepted complexity
envelope. `complexity-tier.json` publishes structural fail-closed caps and the
certified content ceilings (`worst-accepted-b05-v1`: effects/materials, panels,
text labels, per-shot visibility, shots, and text codepoints).

Digests in `b05-workload.lock.json` plus `workload_id` form the baseline
identity. A digest drift without a new workload id fails the offline checksum
audit. Individual run reports stay `certified=false`; the reference series
publishes performance limits without claiming an M1 Air product certificate.
