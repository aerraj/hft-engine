# Design decisions and measured optimizations

Measurements below use identical forced-quote workloads, compiler flags and pinned shared Linux x86-64 runner. Each configuration ran twenty repetitions. These are workload-specific results, not a dedicated-host tuning conclusion. Raw [dense runs](bench/linux/current.json), [sparse runs](bench/linux/sparse.json), and [CI provenance](https://github.com/aerraj/hft-engine/actions/runs/38088662815) are retained.

| Change | Before: sparse-only | After: dense window + sparse fallback | Interpretation |
|---|---:|---:|---|
| Tick-to-trade p50 ns | 310 | 320 | Compare alongside tails; no general speedup claimed |
| Tick-to-trade p99 ns | 330 | 345 | Small differences on a shared VM can be noise |
| Book-processing p99 ns | 60 | 60 | Includes validation and simulator work |
| Book add micro p50 ns (run 1) | 80 | 40 | Existing-price lookup in a 100-level book; per-run JSON available |

Dense storage consumes 32 KiB for the selected instrument, not for every locate. A 4096-tick fixed window falls back to the sparse treap outside its initial range. This makes price bounds explicit without rejecting far prices. The small two-sided end-to-end workload does not establish that dense indexing materially improves whole-engine latency.

## Scan only active simulated orders

The original simulator inspected every one of its 256 slots on relevant book events. A compact active-slot list now visits only outstanding simulated orders, with swap removal on completion. A reference full-scan mode remains available for paired measurements and behavioral tests.

| Same-host measurement | Full scan | Active list |
|---|---:|---:|
| Throughput, million messages/s | 3.523 | 4.709 |
| Tick-to-trade p99 ns | 480 | 345 |
| Book-processing p99 ns | 210 | 60 |

[Full-scan raw runs](bench/linux/fullscan.json) and [active-list raw runs](bench/linux/current.json) use the same two-sided forced-quote v2 workload and compiler barrier around outgoing encoded bytes. Twenty runs per mode; the improvement is specific to this low-live-order workload. The shared-runner throughput remains below the 5 million messages/s acceptance target; dedicated-host validation is outstanding.

## Foundational decisions without fabricated before/after claims

| Decision | Reason | Validation |
|---|---|---|
| Shared object pools and backward-shift ID index | Bound memory; avoid accumulating tombstones | Randomized reference-book tests, capacity/reuse tests, allocation audit |
| Cached best prices and intrusive FIFO | Constant-time touch access and explicit priority | Golden snapshots and queue-position tests |
| Single hot thread/direct calls | Deterministic order and simple ownership | Identical logs on two million-message replays per strategy |
| SPSC binary logger and separate control thread | No formatting or disk I/O in processing | Concurrent ring/logger tests and TSan |
| Explicit endian fields and exact message sizes | No aliasing/alignment UB | Parser tests, malformed-input fuzzing, sanitizers |
| Pending exposure in risk | Cancels and acknowledgments can race with fills | Fill/cancel/replace/kill tests |
| OUCH bytes delivered through the simulator | Exercise encoding/decoding end to end | Golden offsets, response round trips, TCP loopback |
| Separate benchmark and sanitizer binaries | Allocator overrides conflict with TSan runtimes | GCC/Clang sanitizer CI and independent allocation-audited benchmarks |

These are initial architecture choices, not measured speedups over absent alternatives. Future optimization entries must include paired raw runs, source digest/commit, compiler flags, CPU/kernel/affinity, dataset hash and p50/p99/p99.9. Do not compare the original no-order harness to the current order-producing workload. Huge-page advice/mlock and LTO are available but no standalone speedup is claimed for them. PGO and kernel bypass remain optional future experiments.
