#pragma once
#include "hft/core/clock.hpp"
#include "hft/gateway/encoder.hpp"
#include "hft/log/histogram.hpp"
#include "hft/risk/risk.hpp"
#include "hft/sim/exchange.hpp"
#include "hft/strategy/strategies.hpp"
namespace hft {
struct Config {
    int core = -1;
    bool full_scan = false;
    bool dense_index = true, lock_pages = false, huge_pages = false;
    std::uint32_t order_capacity = 262144, level_capacity = 65536;
    Locate target = 1;
    Latency latency{};
    Limits limits{};
    Time quote_interval = 100000;
    Qty strategy_size = 10;
    Price half_spread = 1, skew = 1;
    double threshold = .2;
};
struct NullSink {
    bool wire(Time, const Wire &) noexcept {
        return true;
    }
    bool fill(const Fill &, const Oms &) noexcept {
        return true;
    }
    bool sample(Time, const Book &, const Oms &, Locate) noexcept {
        return true;
    }
};
template <Strategy S> class Engine {
    Config config_;
    Clock *clock_;
    Metrics *metrics_;
    Time now_ = 0, last_quote_ = 0;
    bool healthy_ = true, trading_ = false;
    std::uint64_t encoded_ = 0;
    template <class Sink> bool emit(Time now, const Wire &w, Sink &sink) noexcept {
        if (!sink.wire(now, w)) {
            healthy_ = false;
            return false;
        }
        ++encoded_;
        return true;
    }
    template <class Sink> bool cancel(OrderId id, Sink &sink) noexcept {
        auto *r = oms.get(id);
        if (!r || !oms.request_cancel(id))
            return true;
        if (!emit(now_, encode_cancel(id), sink) || !exchange.cancel(r->order, now_)) {
            healthy_ = false;
            return false;
        }
        return true;
    }
    void record(Metrics::Stage stage, std::uint64_t start, std::uint64_t end) noexcept {
        if (metrics_)
            metrics_->hist[stage].record(clock_->elapsed(start, end));
    }
    std::uint64_t stamp() noexcept {
        return metrics_ ? clock_->stamp() : 0;
    }

  public:
    Book book;
    S strategy;
    Risk risk;
    Oms oms;
    Exchange exchange;
    std::uint64_t messages = 0, book_events = 0, risk_rejections = 0, timestamp_clamps = 0;
    Wire last{};
    explicit Engine(Config c = {}, Clock *clock = nullptr, Metrics *metrics = nullptr)
        : config_(c), clock_(clock), metrics_(metrics),
          book(c.order_capacity, c.level_capacity, c.target, c.dense_index), risk(c.limits),
          exchange(c.latency, c.full_scan) {
        if (metrics_ && !clock_)
            healthy_ = false;
        if constexpr (requires { strategy.size = c.strategy_size; })
            strategy.size = c.strategy_size;
        if constexpr (std::same_as<S, Maker>) {
            strategy.half_spread = c.half_spread;
            strategy.skew_ticks = c.skew;
        } else if constexpr (std::same_as<S, Taker>)
            strategy.threshold = c.threshold;
    }
    bool healthy() const noexcept {
        return healthy_ && exchange.good();
    }
    Time now() const noexcept {
        return now_;
    }
    std::uint64_t encoded() const noexcept {
        return encoded_;
    }
    template <class Sink> bool advance(Time now, Sink &sink) noexcept {
        if (now < now_)
            return false;
        now_ = now;
        auto ok = exchange.advance(now, book, oms, [&](const Fill &f) {
            strategy.on_fill(f);
            if (!sink.fill(f, oms))
                healthy_ = false;
        });
        if (!ok)
            healthy_ = false;
        return healthy();
    }
    template <class Sink> bool replace(OrderId old, Order o, Sink &sink) noexcept {
        auto loc = o.locate;
        auto &inst = book.instrument(loc);
        auto reference = inst.last_trade;
        auto bid = book.best(Side::Buy, loc), ask = book.best(Side::Sell, loc);
        if (!reference && bid && ask)
            reference = (bid + ask) / 2;
        if (risk.check(o, oms, reference, now_, trading_ && inst.status == 'T') != RiskResult::Pass)
            return false;
        auto id = oms.begin_replace(old, o, now_);
        if (!id)
            return false;
        o.id = id;
        risk.committed();
        return emit(now_, encode_replace(old, o), sink) && exchange.replace(old, o, now_);
    }
    template <class Sink> bool kill(Sink &sink) noexcept {
        risk.kill();
        oms.each([&](Oms::Record &r) {
            if (!r.cancel_pending)
                cancel(r.order.id, sink);
        });
        return healthy();
    }
    template <class Sink> bool process(std::span<const unsigned char> packet, Sink &sink) noexcept {
        if (!healthy())
            return false;
        auto begin = stamp();
        Event e;
        if (!parse(packet, e)) {
            healthy_ = false;
            return false;
        }
        auto parsed = stamp();
        record(Metrics::Parse, begin, parsed);
        if (e.time < now_) {
            e.time = now_;
            ++timestamp_clamps;
        }
        if (!advance(e.time, sink))
            return false;
        ++messages;
        auto book_start = stamp();
        // Validate a removal before exposing it to the simulation.
        if (e.kind == Kind::Cancel || e.kind == Kind::Execute || e.kind == Kind::Delete ||
            e.kind == Kind::Replace) {
            auto *n = book.order(e.id);
            if (!n || book.level(n->level).locate != e.locate ||
                ((e.kind == Kind::Cancel || e.kind == Kind::Execute) && e.qty > n->qty)) {
                healthy_ = false;
                return false;
            }
            exchange.on_event(e, book);
        }
        if (!book.apply(e)) {
            healthy_ = false;
            return false;
        }
        auto updated = stamp();
        record(Metrics::BookUpdate, book_start, updated);
        if (e.kind == Kind::System) {
            if (e.code == 'Q')
                trading_ = true;
            else if (e.code == 'M' || e.code == 'E' || e.code == 'C' || e.code == '!') {
                trading_ = false;
                oms.each([&](Oms::Record &r) { cancel(r.order.id, sink); });
            }
        }
        auto loc = config_.target;
        auto &inst = book.instrument(loc);
        if (e.locate == loc && e.kind == Kind::Status && inst.status != 'T')
            oms.each([&](Oms::Record &r) { cancel(r.order.id, sink); });
        if (e.kind == Kind::Execute && e.locate == loc && e.printable)
            strategy.on_trade(inst.last_trade, e.qty);
        if (e.kind == Kind::Trade && e.locate == loc && e.printable)
            strategy.on_trade(e.price, e.qty);
        strategy.on_timer(now_);
        bool relevant = e.locate == loc &&
                        (e.kind == Kind::Add || e.kind == Kind::Cancel || e.kind == Kind::Delete ||
                         e.kind == Kind::Replace || e.kind == Kind::Execute);
        if (relevant)
            ++book_events;
        if (relevant && trading_ && inst.status == 'T' &&
            now_ - last_quote_ >= config_.quote_interval) {
            last_quote_ = now_;
            auto signal_start = stamp();
            auto intent = strategy.on_book_update(book, loc, oms.account(loc).position);
            if (intent.count > intent.orders.size()) {
                healthy_ = false;
                return false;
            }
            auto signal_end = stamp();
            record(Metrics::Signal, signal_start, signal_end);
            // Maintain at most one quote per side. Cancel stale quotes before submitting
            // replacements; until cancel acknowledgment they remain in risk exposure and self-trade
            // checks.
            {
                oms.each([&](Oms::Record &r) {
                    if (r.order.ioc)
                        return;
                    bool desired = false;
                    for (unsigned i = 0; i < intent.count; ++i)
                        if (r.order.side == intent.orders[i].side &&
                            r.order.price == intent.orders[i].price &&
                            r.order.qty - r.filled == intent.orders[i].qty)
                            desired = true;
                    if (!desired)
                        cancel(r.order.id, sink);
                });
            }
            for (unsigned i = 0; i < intent.count; ++i) {
                auto o = intent.orders[i];
                if (o.locate != loc) {
                    healthy_ = false;
                    return false;
                }
                bool duplicate = false;
                oms.each([&](const Oms::Record &r) {
                    if (r.order.locate == loc && r.order.side == o.side)
                        duplicate = true;
                });
                if (duplicate)
                    continue;
                auto risk_start = stamp();
                auto bid = book.best(Side::Buy, loc), ask = book.best(Side::Sell, loc);
                auto reference =
                    inst.last_trade ? inst.last_trade : (bid && ask ? (bid + ask) / 2 : 0);
                auto result = risk.check(o, oms, reference, now_, trading_ && inst.status == 'T');
                auto risk_end = stamp();
                record(Metrics::RiskCheck, risk_start, risk_end);
                if (result != RiskResult::Pass) {
                    ++risk_rejections;
                    continue;
                }
                auto id = oms.submit(o, now_);
                if (!id) {
                    healthy_ = false;
                    return false;
                }
                o.id = id;
                risk.committed();
                auto encode_start = stamp();
                last = encode(o, inst.symbol);
                auto encoded = stamp();
                record(Metrics::Encode, encode_start, encoded);
                record(Metrics::TickToTrade, parsed, encoded);
                if (!emit(now_, last, sink) ||
                    !exchange.submit_wire(last, loc, inst.symbol, now_)) {
                    healthy_ = false;
                    return false;
                }
            }
        }
        if (!advance(now_, sink))
            return false;
        if (relevant && !sink.sample(now_, book, oms, loc)) {
            healthy_ = false;
            return false;
        }
        record(Metrics::Total, begin, stamp());
        return healthy();
    }
    bool process(std::span<const unsigned char> packet) noexcept {
        NullSink sink;
        return process(packet, sink);
    }
    template <class Sink> bool finish(Sink &sink) noexcept {
        auto delay = config_.latency.network * 2 + config_.latency.exchange + 1;
        if (!advance(now_ + delay, sink) || !kill(sink) || !advance(now_ + delay, sink))
            return false;
        return oms.active() == 0 && healthy();
    }
};
} // namespace hft
