# hft-engine

![Latency histogram](docs/bench/macos-dense.svg)

```mermaid
flowchart LR
  F[ITCH file or MoldUDP64] --> P[Validated ITCH parser]
  P --> B[Instrument books: pools + FIFO + dense/sparse prices]
  B --> S[Microprice maker / EMA imbalance taker]
  S --> R[Position + pending exposure + collars + rate + self-trade]
  R --> O[OMS state machines]
  O --> G[OUCH 4.2 encoder]
  G --> X[Latency / price-time / queue simulator]
  X --> A[OUCH acknowledgments and fills]
  A --> O
  O --> L[SPSC binary logger]
  C[Unix control: kill / limits / stats] --> R
```

C++20 single-process simulation engine. Replay and UDP use the same parser, books, strategies, risk, OMS and simulator. Public GitHub repository; no real-money/exchange connectivity.

The implementation includes all displayed-book ITCH mutations, all standard administrative layouts, instrument routing, pooled FIFO storage, dense/sparse prices, two reference strategies, pending-exposure risk, cancel/replace races, OUCH messages, SoupBinTCP session/loopback transport, latency and queue models, deterministic logs, binary snapshots/recovery, async logging, Linux affinity/huge-page advice/mlock options, HdrHistogram and allocation-audited benchmarks.

**Validation boundaries:** the committed local measurements are macOS development diagnostics. Dedicated Linux x86-64 latency targets and exchange-certified end-of-day book equality remain unverified. CI publishes Linux benchmark artifacts. The million-message stress test is synthetic, not a full historical NASDAQ day. The simulator is an explicit counterfactual fill model, not evidence of profitability.

## Build and run

```sh
cmake --preset debug
cmake --build build/debug -j 4
ctest --test-dir build/debug --output-on-failure
python3 tools/generate_fixture.py
build/debug/hft_replay maker tests/fixtures/synthetic.itch build/debug/maker --metrics
build/debug/hft_replay taker tests/fixtures/synthetic.itch build/debug/taker --metrics
python3 tools/report.py build/debug/maker.orders --out build/debug/maker-report
```

CMake ≥3.24, Ninja, C++20, POSIX and Python ≥3.9. GCC 14 and Clang 18 CI builds. Dependencies are pinned to immutable commits via CMake; optional vcpkg manifest supplies GoogleTest/Benchmark. Presets cover Debug, RelWithDebInfo, Release+LTO, ASan/UBSan and TSan. `-fno-exceptions -fno-rtti` applies to engine targets; `-march=native` is benchmark-only.

## Benchmarks

```sh
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DHFT_NATIVE=ON
cmake --build build/bench -j 4
# Linux, available isolated core 2:
python3 tools/run_benchmarks.py build/bench/hft_bench --core 2 --out latency --repetitions 20
python3 tools/latency_report.py latency
build/bench/hft_microbench --benchmark_repetitions=20
```

The benchmark forces outgoing quotes, measures p50/p90/p99/p99.9/max per stage and audits C++ heap allocations after warm-up. Throughput is measured separately without timestamp/histogram overhead. The report records timer/compiler/platform, native flags and source digest. Fixed-seed replay determinism excludes wall-clock performance records.

See [benchmark artifacts](docs/bench/), [design/complexities](docs/design.md), [decisions with paired measurements](docs/decisions.md), [backtest reports](docs/backtest.md), [implementation write-up](docs/building-sub-5us.md), [operations and data](docs/operations.md), and [reading list](docs/reading-list.md).

CI runs randomized book/reference tests, golden replay, byte-identical logs, ASan/UBSan, TSan, fuzzing and UDP/TCP loopback. Shared-runner benchmark checks are smoke tests. The 10% p99 regression gate requires a dedicated self-hosted runner and host-matched baseline (see operations).
