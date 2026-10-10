# Strategy backtest and determinism report

**Synthetic engineering validation, not historical trading performance.** The 10026-message, two-symbol fixture covers administrative records and displayed-book mutations, then 1000 deterministic market cycles. Price units are 1/10000 dollar. One-way network latency = 1000 ns, exchange latency = 1000 ns; no fees by default. Maker quotes around microprice with inventory skew; taker uses an incremental EMA of touch imbalance.

| Strategy | Orders | Fill records | Share fill rate | Net PnL ticks | End inventory | Max abs inventory | Adverse selection cost ticks |
|---|---:|---:|---:|---:|---:|---:|---:|
| Maker | 3490 | 1010 | 28.94% | 24440 | -100 | 100 | -1.980 |
| Imbalance taker | 104 | 104 | 100.00% | -1820 | -1000 | 1000 | 2.981 |

Adverse selection is negative signed future-mark markout (midpoint when two-sided, otherwise the last printable trade) at a 1 ms horizon; negative cost means favorable markout. PnL includes end inventory marked at midpoint where two-sided, otherwise the last printable trade. Fees can be applied with `--fee-ticks`. The [maker](backtests/maker.json) and [taker](backtests/taker.json) JSONs include realized PnL, fill metrics and nonannualized one-second PnL-increment Sharpe. There are only ten return observations here; an annualized Sharpe is deliberately not estimated. Full order/fill/inventory CSVs are committed alongside the JSON reports.

The taker saturates its position limit on this fixture; risk then rejects further orders. This is visible in replay summaries and is not hidden by assumed liquidation. Queue-aware fills, latency and consumed-depth accounting are active in both runs. Counterfactual historical depth remains an approximation: fees, market impact, hidden liquidity and venue queue exceptions need calibration before economic use.

## Walk-forward demonstration

Train on an independent synthetic session with seed 7; select only on training PnL. Evaluate once on a held-out independent session with seed 11. Grid: maker half-spread 1/2/3 ticks; taker imbalance threshold 1000/2000/3000 basis points. Sessions reset book and inventory. This demonstrates the workflow without claiming robust out-of-sample profitability.

| Strategy | Training-selected parameter | Training PnL ticks | Held-out PnL ticks |
|---|---:|---:|---:|
| maker | 1 | 24440 | 24020 |
| taker | 3000 | 0 | 0 |

[Maker fold](backtests/maker-walk-forward.json) · [Taker fold](backtests/taker-walk-forward.json).

## Reproduce

```sh
python3 tools/generate_fixture.py
build/release/hft_replay maker tests/fixtures/synthetic.itch build/release/maker
build/release/hft_replay taker tests/fixtures/synthetic.itch build/release/taker
python3 tools/report.py build/release/maker.orders --out build/release/maker-report
python3 tools/report.py build/release/taker.orders --out build/release/taker-report
python3 tools/generate_fixture.py --out build/release/held-out.itch --seed 11
python3 tools/walk_forward.py build/release/hft_replay maker tests/fixtures/synthetic.itch build/release/held-out.itch --out build/release/maker-fold.json
python3 tools/walk_forward.py build/release/hft_replay taker tests/fixtures/synthetic.itch build/release/held-out.itch --out build/release/taker-fold.json
```

## Stress and book correctness

Both strategies completed **1000026 synthetic messages**, twice each, with byte-identical binary logs. The [stress manifest](backtests/stress.json) records dataset size/hash, per-strategy counts and log hashes. The fixture repeatedly replenishes only a small live book; this tests sustained replay and logger drainage, not maximum universe occupancy. Separate randomized tests exercise larger books, sparse prices, hash/pool reuse and capacity failures.

Golden replay compares independently rebuilt book snapshots every 1000 records. Randomized book streams are checked against a separate map implementation. Unit/property, sanitizer and UDP/TCP control/session tests run in CI. A full historical day and an exchange end-of-day reference snapshot have not been supplied or validated; the public-data downloader and configurable capacities provide the reproduction path.
