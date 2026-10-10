# Building a sub-5 µs trading engine in C++

The useful milestone is a working, measurable system: feed bytes become an instrument book update, a strategy decision, a reserved risk exposure, an order token and an OUCH message. Replay and the UDP runner share that chain; no shadow backtest implementation is allowed to drift from it.

## Start with correctness

ITCH decoding uses explicit endian reads instead of packed-struct overlays. Exact lengths, valid sides, timestamp bounds, known IDs and quantity invariants reject malformed input before book mutation. Shared pools bound memory, while a backward-shift hash index avoids accumulating tombstones over an entire day. Intrusive FIFO lists preserve order priority. Dense prices speed ordinary lookup; sparse treaps preserve support for far prices without widening an enormous array.

Random streams are checked against a separate map-based model. Golden snapshots independently reconstruct the generated multi-instrument feed. Two runs must emit byte-identical binary order/fill/inventory logs, including a million-message stress run. Simulator clocks come from the feed; wall-clock measurements live in separate files.

## Risk is about what can still fill

A pending acknowledgment and a cancel in flight still represent exposure. Position limits reserve buy and sell quantities independently, rather than assuming they net. The OMS accepts a partial fill while cancellation is pending and accounts for it before retiring the order. Replacement gets a new token and loses priority; old fills reduce replacement quantity. A completed old order wins the race. These cases have tests because a fast engine that miscounts exposure is unusable.

## Model the queue explicitly

Simulated market orders consume available historical depth once. Subsequent historical removals absorb that simulated consumption, and genuinely new adds restore availability. Passive quotes join behind the visible queue at arrival after configured network/exchange latency. Cancels ahead reduce that queue; trades on later orders can fill our quote once its queue is clear. Own quotes share a FIFO fill budget.

This is a counterfactual model, not a reconstruction of what an exchange would certainly have done. Hidden liquidity, routing, queue exceptions, market impact and fees require calibration. Synthetic positive PnL demonstrates accounting and fills; it is not a trading result.

## Measure an order-producing workload

The benchmark deliberately changes the touch and forces new quotes. Per-stage HdrHistograms capture parsing, book processing, signal, risk, encoding and tick-to-trade. The core is pinned on Linux, invariant TSC is calibrated at startup, and every raw repetition is retained. A separate run measures throughput without instrumentation. Overridden C++ allocation operators audit the warmed processing loop. Logger I/O, sockets and timer overhead are not silently treated as free: the documentation defines the measurement boundary.

The public CI smoke run on Linux x86-64 reports sub-microsecond median tick-to-trade for this small synthetic workload. Its shared virtual machine is not a dedicated low-latency host; worst-case scheduler stalls remain possible. See the README and raw reports for exact numbers and environment. Dedicated-host real-data acceptance and wire-to-wire NIC timestamps are separate milestones, not claims inferred from a fast median.

## Keep optimizations accountable

Dense lookup versus sparse-only lookup is a paired runtime choice with identical workload and compiler. If p99 is unchanged, the decision log says so. Background logging and control stay off the selected hot core. No hot-path allocation, formatting, exceptions, virtual dispatch or disk reads are needed. Further changes—PGO, huge pages, kernel bypass or splitting cores—should only follow a measured bottleneck.
