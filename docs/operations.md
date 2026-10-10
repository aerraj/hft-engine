# Operations, reproduction and limits

## Replay

`hft_replay maker|taker file.itch output_prefix` writes `.orders` (portable binary records), `.json` summary, `.book.csv`, and a recoverable `.snapshot`. `--metrics` adds `.latency.json/.csv`. `--snapshot-every N` writes intermediate book CSVs for golden verification. These optional snapshots run at the cold replay boundary and are excluded from the processing benchmark.

Startup configuration: `--config config/default.conf` loads validated integer `key=value` settings before trading. Flags apply in order, so later flags override configuration values. Unknown/out-of-range configuration entries reject the entire load. No disk read occurs in processing.

Options: `--locate`, `--orders` (pool capacity), `--levels`, `--network-ns`, `--exchange-ns`, `--quote-ns`, `--size`, `--half-spread`, `--skew`, `--threshold-bps`, `--core`, `--mlock`, `--huge-pages`. Risk limits are also available through the C++ `Config` and live control channel. Prices are exchange units: $1 = 10,000 ticks. The default pools support 262144 historical orders and 65536 levels, shared across instruments; large days require appropriately sized pools.

```sh
python3 tools/download_data.py --file 12302019.NASDAQ_ITCH50.gz --out data/day.itch
build/release/hft_replay maker data/day.itch run/maker --locate 123 --orders 16000000 --levels 4000000
```

Public files are several GB compressed. The downloader verifies the exchange MD5, records SHA-256 and decompresses locally. Raw data is not committed or redistributed. Select the day's locate from its directory; locate IDs change by day. Capacity above is illustrative, not a certified upper bound. Full-day exchange end-of-day validation requires an independent exchange reference snapshot. No such validation is claimed by synthetic fixtures.

## UDP against simulator

```sh
build/debug/hft_live maker 18000 run/live.orders /tmp/hft-control.sock 0 --locate 1
python3 tools/control.py /tmp/hft-control.sock stats
python3 tools/control.py /tmp/hft-control.sock hist
python3 tools/control.py /tmp/hft-control.sock limits 100 1000 100000000 1000 10000
python3 tools/control.py /tmp/hft-control.sock kill
```

Count zero runs until SIGINT/SIGTERM; positive count bounds the number of feed messages. `--group` joins multicast, `--recovery` points to a local snapshot, and `--core` reserves a hot core while background threads exclude it. Control replies `queued` before the main loop applies a change; stats are a recent atomic counter snapshot. Kill is latched; restarting deliberately is required to resume strategy submissions. Latency histograms dump on shutdown or through `hist`. On-demand snapshots copy preallocated counters at a loop boundary, then format the reply on the control thread; this deliberate diagnostic request can briefly perturb latency. Unix socket paths must be unused; no existing path is deleted on startup.

`TcpGateway` is a loopback-only SoupBinTCP transport for simulator integration, including fragmented/coalesced frames, login, heartbeats, timeout detection and reconnect sequence preservation. The caller retains pending order tokens and can resend after login. It is not an exchange connection client.

## Linux measurement

Use a dedicated x86-64 host, invariant TSC, fixed governor, isolated hot core, IRQ affinity away from it and a documented SMT policy. Record CPU/kernel/compiler/governor/power state and data checksums. Pin with `--core`, warm caches, run at least twenty repetitions, retain every raw file. `--huge-pages` requests THP advice; it does not prove physical huge-page backing. `--mlock` requires adequate memlock limits. Do not use sanitizers for latency measurement.

Shared GitHub Linux runners validate builds and publish smoke measurements; their latency tails are not dedicated-host acceptance evidence. To enable the regression job, provision a Linux x64 runner labeled `hft-benchmark`, place a reviewed host-matched aggregate JSON at `/opt/hft-baseline/latency.json`, then set repository variable `BENCHMARK_RUNNER=true`. The comparator rejects incompatible metadata, missing samples, heap allocations, no outgoing orders, and any stage's p99 exceeding baseline by 10%. It never auto-updates the baseline.

## Testing and external boundaries

`ctest` runs unit/property, golden replay/determinism, and UDP loopback. The GoogleTest suite includes fragmented TCP/Soup/OUCH loopback, queue/latency/consumption, cancel/replace races, snapshots, pool/hash reuse, accounting and risk. The libFuzzer target requires a compiler distribution with its runtime; Apple's Command Line Tools currently omit that runtime, so local malformed-input smoke fuzzing uses ASan/UBSan and Linux CI runs libFuzzer. Dependencies are fetched at fixed commits; builds may need network access initially.

Targets (<100 ns median book update, <5 µs median / <20 µs p99.9 tick-to-trade, ≥5M messages/s, no post-warmup heap allocations) are acceptance gates for a specified dedicated Linux host and real message distribution. No NIC hardware timestamps or wire-to-wire measurement is included. Kernel bypass, multi-venue routing, real money and a GUI remain outside v1.
