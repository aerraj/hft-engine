# Building a sub-5 µs trading engine in C++

This is a target and a development log, not an achieved performance claim.

The first milestone is deterministic replay through the same parser, book, strategy, risk and encoding functions. Fixed arrays bound storage, integer ticks avoid rounding, and explicit big-endian decoding avoids unaligned struct overlays. Invalid updates stop replay rather than allowing a corrupted book to continue.

A tiny bounded book makes the initial implementation inspectable. It also exposes tradeoffs: touch lookup scans prices and tombstones lengthen hash probes. Before replacing either, benchmark realistic occupancy and message distributions on an isolated Linux x86-64 core.

The current macOS benchmark has no opposite touch, hence no outgoing order. Its histogram is useful for checking the harness but does not measure tick-to-trade. The next benchmark must force an order, timestamp every stage, audit allocations and report distributions over at least twenty runs. Only then can the title become an achievement.

Strategy credibility is a separate milestone. Immediate fills against unconsumed historical depth are optimistic; a queue-aware simulator must reconcile historical executions, latency and our own resting orders. Until then synthetic PnL validates bookkeeping, not an investment thesis.
