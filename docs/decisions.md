# Design decisions

| Decision | Reason | Before | After |
|---|---|---|---|
| Single-thread direct calls | Deterministic component reuse | Not measured | Initial prototype only |
| Fixed arrays and integer prices | Bounded memory and exact tick arithmetic | Not measured | Allocation audit pending |
| Explicit endian reads | Avoid alignment and aliasing UB | No alternate parser | A/X/E parser tested |
| Fail on unsupported input | Avoid silently corrupting book | No baseline | Replay exits nonzero |
| Keep native CPU tuning in benchmark | Portable CI executables | Not measured | Build flags inspected |

No optimization speedup is claimed without paired before/after measurements. Future entries must include commit IDs, compiler flags, CPU, kernel, affinity, dataset hash, repetitions, p50/p99/p99.9 and raw artifacts. Dedicated Linux baselines remain outstanding.
