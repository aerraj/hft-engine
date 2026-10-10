# Design and semantics

Single producer drives parser → book → strategy → risk → OMS → encoder. The simulator decodes actual OUCH enter bytes and delivers encoded OUCH acknowledgments/executions/cancels/replacements through the report decoder. The same `Engine<Strategy>` runs in replay and UDP modes. Only the I/O/clock boundary changes. Exactly one selected locate runs the strategy per process; every feed instrument's displayed book is reconstructed in shared pools.

## Storage and complexity

Orders use a fixed index pool, an open-addressed ID index with backward-shift deletion, and intrusive doubly linked FIFO lists. Shared level pools back per-instrument deterministic treaps across the full valid integer price range. The selected locate has a 4096-price dense lookup window anchored on its first price. Prices outside it and other instruments use the sparse tree. Capacity is configured before replay; exhaustion is a visible fatal error, never a silently dropped message.

| Operation | Complexity |
|---|---|
| ID lookup | expected O(1), bounded by hash table capacity |
| Partial cancellation/execution | expected O(1) |
| Add to dense existing level | expected O(1) |
| Create/remove sparse level | expected O(log price levels) |
| Best price, touch quantity, touch imbalance | O(1) cached |
| N-level depth | O(N log price levels) |
| Queue shares ahead of arbitrary historical ID | O(orders ahead), exact FIFO sum |

Queue position is explicitly not claimed O(1): exact prefix quantities change when earlier orders cancel. The simulator stores a historical queue watermark and queue-ahead quantity for its bounded active orders instead.

## Market data

ITCH lengths and endian offsets follow the linked NASDAQ 5.0 specification. A/F adds, X partial cancels, D deletes, U replaces, E/C executions update displayed books. P/Q trades, B broken trades and all documented administrative/NOII/RPI messages are parsed/validated; P can update trade signals, crosses and broken trades do not restore displayed orders. Directory and trading actions route by locate; trading requires system market-open and instrument `T` status. Unknown types, invalid sizes and impossible updates stop processing.

Historical record order is authoritative. Nonmonotonic feed timestamps are clamped to the current simulation clock; this preserves every message rather than rolling event time backward. Mold validates a whole datagram before application, detects sequence/session changes and latches a gap. Recovery requires a complete local snapshot with its next sequence; new trading stays killed after recovery. This is a local snapshot interface, not a GLIMPSE network client.

## Order lifecycle and risk

OMS tracks pending acknowledgment, live, partially filled and terminal orders. Cancellation is an orthogonal pending flag: fills while a cancel is in flight still update cash and inventory. Partial cancels reduce remaining exposure. Atomic replacements receive new tokens and lose FIFO priority; fills before replacement reduce the replacement's remaining shares. A filled old order wins the replacement race. Records are bounded and stale IDs cannot mutate reused slots.

Risk reserves all remaining pending/live/cancel-pending quantity. Checks cover order size, worst-case long/short position, marked position plus open-order notional, last-trade/midpoint collar, per-second new/replace rate and self-crossing against own active orders. Kill latches, cancels every active order and blocks submissions. Cancels bypass new-order rate limits so kill is never throttled. Limits/config are applied at startup or event boundaries, never read from disk in the processing function.

## Simulator assumptions

New/cancel/replace arrive after configured one-way network plus exchange delay; acknowledgments/fills return after another network delay. Equal-time messages use an explicit serial tie breaker. Aggressive fills consume displayed liquidity in price order, including walking multiple levels. A consumption ledger prevents buying the same historical shares repeatedly; later historical removals absorb consumed quantity, and fresh adds contribute new liquidity.

Passive orders join behind displayed shares at arrival. Historical cancels ahead reduce queue ahead; later historical orders join behind. Executions of later historical orders can fill simulated quotes after their historical queue clears. Own orders at a level fill in token/FIFO order. This is a deterministic conservative approximation: order-level market data cannot identify all counterfactual hidden liquidity, routing, priority exemptions or market impact. Calibration against exchange fills remains external validation.

## Threads and logs

One hot thread, one SPSC binary logger and one control thread in UDP mode. `--core` pins only the hot thread and excludes that core from background workers on Linux; do not pin the whole process with taskset when using logging/control. Binary records have explicit endian fields with no compiler padding. Overflow/disk failures are visible. Replay applies backpressure before entering the processing function; UDP fails closed if logging cannot keep up.

The logger writes feed/simulation timestamps, making order/fill/inventory logs byte-identical across replays. HdrHistogram records performance clock values separately. Invariant TSC is checked and calibrated at startup on x86-64; steady_clock is the fallback. Histogram allocation occurs at startup. No virtual dispatch, exceptions, RTTI, formatting or disk I/O occurs in `Engine::process`.
