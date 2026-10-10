#include "hft/core/config.hpp"
#include "hft/core/engine.hpp"
#include "hft/feed/recovery.hpp"
#include "hft/feed/replay.hpp"
#include "hft/log/sink.hpp"
#include <charconv>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
namespace {
bool number(std::string_view s, std::uint64_t &v) {
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
void snapshot_csv(const hft::Book &b, const std::string &path) {
    std::ofstream out(path);
    out << "locate,side,price,id,qty,queue_ahead\n";
    b.each_order([&](hft::Locate loc, hft::Side s, hft::Price p, const hft::Book::Node &n) {
        out << loc << ',' << (s == hft::Side::Buy ? 'B' : 'S') << ',' << p << ',' << n.id << ','
            << n.qty << ',' << b.queue_ahead(n.id) << '\n';
    });
}
template <class S>
int run(const std::string &input, const std::string &prefix, hft::Config c, bool metrics,
        unsigned snapshot_every) {
    auto clock = std::make_unique<hft::Clock>();
    auto m = std::make_unique<hft::Metrics>();
    auto e = std::make_unique<hft::Engine<S>>(c, clock.get(), metrics ? m.get() : nullptr);
    auto logger = std::make_unique<hft::AsyncLogger>((prefix + ".orders").c_str(), c.core);
    hft::BinarySink sink{*logger};
    if (c.core >= 0) {
        if (!hft::pin_core(static_cast<unsigned>(c.core)))
            return 2;
        *clock = hft::Clock{};
    }
    if (c.huge_pages && !e->book.huge_pages()) {
        std::cerr << "Huge-page advice unavailable\n";
        return 2;
    }
    if (c.lock_pages && !hft::lock_memory()) {
        std::cerr << "mlockall unavailable\n";
        return 2;
    }
    hft::ReplayReader reader(input);
    std::span<const unsigned char> packet;
    while (reader.next(packet)) {
        while (!logger->ready()) {
            if (!logger->good())
                return 6;
            std::this_thread::yield();
        }
        if (!e->process(packet, sink)) {
            std::cerr << "Replay failed at record " << reader.records()
                      << " (malformed feed, invalid book update, capacity or logging failure)\n";
            return 4;
        }
        if (snapshot_every && reader.records() % snapshot_every == 0)
            snapshot_csv(e->book,
                         prefix + ".snapshot." + std::to_string(reader.records()) + ".csv");
    }
    if (!reader.good()) {
        std::cerr << "Invalid/truncated input\n";
        return 3;
    }
    if (!e->finish(sink)) {
        std::cerr << "Simulation could not drain orders\n";
        return 5;
    }
    if (!sink.sample(e->now(), e->book, e->oms, c.target))
        return 6;
    logger->close();
    if (!logger->good())
        return 6;
    snapshot_csv(e->book, prefix + ".book.csv");
    if (!hft::save_snapshot((prefix + ".snapshot").c_str(), e->book, reader.records() + 1))
        return 7;
    auto bid = e->book.best(hft::Side::Buy, c.target),
         ask = e->book.best(hft::Side::Sell, c.target);
    auto mark = bid && ask ? (bid + ask) / 2 : e->book.instrument(c.target).last_trade;
    auto &a = e->oms.account(c.target);
    std::ofstream summary(prefix + ".json");
    summary << "{\"messages\":" << e->messages << ",\"book_events\":" << e->book_events
            << ",\"book_orders\":" << e->book.size() << ",\"peak_book_orders\":" << e->book.peak()
            << ",\"orders\":" << e->oms.submitted << ",\"fills\":" << e->oms.fill_count
            << ",\"fill_qty\":" << e->oms.fill_qty << ",\"cancelled\":" << e->oms.cancelled
            << ",\"risk_rejections\":" << e->risk_rejections << ",\"position\":" << a.position
            << ",\"cash_ticks\":" << a.cash << ",\"realized_ticks\":" << a.realized
            << ",\"unrealized_ticks\":" << a.unrealized(mark)
            << ",\"equity_ticks\":" << a.equity(mark)
            << ",\"network_latency_ns\":" << c.latency.network
            << ",\"exchange_latency_ns\":" << c.latency.exchange
            << ",\"deterministic_clock\":true}\n";
    summary.close();
    std::ifstream result(prefix + ".json");
    std::cout << result.rdbuf();
    if (metrics) {
        std::ofstream out(prefix + ".latency.json");
        m->json(out);
        std::ofstream csv(prefix + ".latency.csv");
        m->csv(csv);
    }
    return summary ? 0 : 8;
}
} // namespace
int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "usage: hft_replay maker|taker input.itch output_prefix [--locate N] "
                     "[--network-ns N] [--exchange-ns N] [--quote-ns N] [--orders N] [--levels N] "
                     "[--core N] [--snapshot-every N] [--metrics] [--mlock]\n";
        return 1;
    }
    hft::Config c;
    bool metrics = false;
    unsigned snapshots = 0;
    for (int i = 4; i < argc; ++i) {
        std::string_view key = argv[i];
        if (key == "--config") {
            if (i + 1 >= argc || !hft::load_config(argv[++i], c))
                return 2;
            continue;
        }
        if (key == "--metrics") {
            metrics = true;
            continue;
        }
        if (key == "--mlock") {
            c.lock_pages = true;
            continue;
        }
        if (key == "--huge-pages") {
            c.huge_pages = true;
            continue;
        }
        std::uint64_t n;
        if (i + 1 >= argc || !number(argv[++i], n))
            return 2;
        if (key == "--locate" && n < 65536)
            c.target = static_cast<hft::Locate>(n);
        else if (key == "--network-ns" && n <= 1000000000)
            c.latency.network = n;
        else if (key == "--exchange-ns" && n <= 1000000000)
            c.latency.exchange = n;
        else if (key == "--size" && n > 0 && n < 1000000)
            c.strategy_size = static_cast<hft::Qty>(n);
        else if (key == "--half-spread" && n < 10000)
            c.half_spread = static_cast<hft::Price>(n);
        else if (key == "--skew" && n < 10000)
            c.skew = static_cast<hft::Price>(n);
        else if (key == "--threshold-bps" && n > 0 && n < 10000)
            c.threshold = double(n) / 10000;
        else if (key == "--quote-ns")
            c.quote_interval = n;
        else if (key == "--orders" && n > 0 && n <= 100000000)
            c.order_capacity = static_cast<std::uint32_t>(n);
        else if (key == "--levels" && n > 0 && n <= 10000000)
            c.level_capacity = static_cast<std::uint32_t>(n);
        else if (key == "--snapshot-every" && n <= UINT32_MAX)
            snapshots = static_cast<unsigned>(n);
        else if (key == "--core" && n < 1024) {
            c.core = static_cast<int>(n);
        } else
            return 2;
    }
    if (std::string_view(argv[1]) == "maker")
        return run<hft::Maker>(argv[2], argv[3], c, metrics, snapshots);
    if (std::string_view(argv[1]) == "taker")
        return run<hft::Taker>(argv[2], argv[3], c, metrics, snapshots);
    return 1;
}
