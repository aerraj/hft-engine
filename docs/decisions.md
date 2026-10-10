# Design decisions and measured optimizations

Measurements below use identical forced-quote workloads, compiler flags and pinned shared Linux x86-64 runner. Each configuration ran twenty repetitions. These are workload-specific results, not a dedicated-host tuning conclusion. Raw [dense runs](bench/linux/current.json), [sparse runs](bench/linux/sparse.json), and [CI provenance](https://github.com/aerraj/hft-engine/actions/runs/38088195005) are retained.

| Change | Before: sparse-only | After: dense window + sparse fallback | Interpretation |
|---|---:|---:|---|
| Tick-to-trade p50 ns | 561 | 561 | Compare alongside tails; no general speedup claimed |
| Tick-to-trade p99 ns | 591 | 596 | Small differences on a shared VM can be noise |
| Book-processing p99 ns | 260 | 260 | Includes validation and simulator work |
| Book add micro p50 ns (run 1) | 80 | 40 | Existing-price lookup in a 100-level book; per-run JSON available |

Dense storage consumes 32 KiB for the selected instrument, not for every locate. A 4096-tick fixed window falls back to the sparse treap outside its initial range. This makes price bounds explicit without rejecting far prices. The small two-sided end-to-end workload does not establish that dense indexing materially improves whole-engine latency.

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
