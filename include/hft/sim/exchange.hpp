#pragma once
#include "hft/book/book.hpp"
#include "hft/gateway/reports.hpp"
#include "hft/oms/oms.hpp"
#include <algorithm>
namespace hft {
struct Latency {
    Time network = 1000, exchange = 1000;
};
class Exchange {
    enum class Action : std::uint8_t { New, Cancel, Replace, Ack, Replaced, Fill, Cancelled };
    struct Scheduled {
        Time time{};
        std::uint64_t sequence{};
        Action action{};
        Order order{};
        Fill fill{};
        OrderId previous{};
    };
    struct Resting {
        Order order{};
        Qty remaining{};
        std::uint64_t ahead{}, cutoff{};
    };
    static constexpr std::size_t capacity = 8192;
    std::array<Scheduled, capacity> heap_{};
    std::size_t size_ = 0;
    std::uint64_t sequence_ = 0;
    std::array<Resting, Oms::capacity> resting_{};
    Latency latency_;
    bool good_ = true;
    static bool later(const Scheduled &a, const Scheduled &b) noexcept {
        return a.time > b.time || (a.time == b.time && a.sequence > b.sequence);
    }
    bool schedule(Scheduled s) noexcept {
        if (size_ == capacity) {
            good_ = false;
            return false;
        }
        s.sequence = sequence_++;
        heap_[size_++] = s;
        std::push_heap(heap_.begin(), heap_.begin() + static_cast<std::ptrdiff_t>(size_), later);
        return true;
    }
    void response(Action a, const Order &o, Time t, Fill f = {}, OrderId previous = 0) noexcept {
        schedule({t + latency_.network, 0, a, o, f, previous});
    }

  public:
    explicit Exchange(Latency latency = {}) : latency_(latency) {}
    bool good() const noexcept {
        return good_;
    }
    std::size_t pending() const noexcept {
        return size_;
    }
    bool submit_wire(const Wire &w, Locate loc, Symbol expected, Time now) noexcept {
        Order o;
        Symbol symbol;
        if (!decode_order(w.view(), o, symbol) || symbol != expected)
            return false;
        o.locate = loc;
        return submit(o, now);
    }
    bool submit(const Order &o, Time now) noexcept {
        return schedule({now + latency_.network + latency_.exchange, 0, Action::New, o, {}});
    }
    bool replace(OrderId old, const Order &o, Time now) noexcept {
        return schedule(
            {now + latency_.network + latency_.exchange, 0, Action::Replace, o, {}, old});
    }
    bool cancel(const Order &o, Time now) noexcept {
        return schedule({now + latency_.network + latency_.exchange, 0, Action::Cancel, o, {}});
    }
    template <class OnFill> bool advance(Time now, Book &b, Oms &oms, OnFill &&on_fill) noexcept {
        while (size_ && heap_[0].time <= now) {
            std::pop_heap(heap_.begin(), heap_.begin() + static_cast<std::ptrdiff_t>(size_), later);
            auto s = heap_[--size_];
            auto &o = s.order;
            auto &r = resting_[o.id % Oms::capacity];
            switch (s.action) {
            case Action::New: {
                if (r.remaining) {
                    good_ = false;
                    return false;
                }
                response(s.previous ? Action::Replaced : Action::Ack, o, s.time, {}, s.previous);
                Qty remaining = o.qty;
                auto opposite_side = opposite(o.side);
                auto price = b.best(opposite_side, o.locate);
                while (price && remaining &&
                       (o.side == Side::Buy ? price <= o.price : price >= o.price)) {
                    auto q = b.consume(o.locate, opposite_side, price, remaining);
                    if (q) {
                        response(Action::Fill, o, s.time,
                                 {o.id, q, price, s.time + latency_.network, false});
                        remaining -= q;
                    }
                    price = b.next_price(o.locate, opposite_side, price);
                }
                if (remaining && !o.ioc) {
                    std::uint64_t ahead = b.depth(o.side, o.price, o.locate);
                    r = {o, remaining, ahead, b.serial()};
                } else if (remaining)
                    response(Action::Cancelled, o, s.time);
                break;
            }
            case Action::Replace: {
                auto &old = resting_[s.previous % Oms::capacity];
                if (!old.remaining || old.order.id != s.previous) {
                    response(Action::Cancelled, o, s.time);
                    break;
                }
                auto executed_qty = old.order.qty - old.remaining;
                old = {};
                o.qty = o.qty > executed_qty ? o.qty - executed_qty : 0;
                if (!o.qty) {
                    response(Action::Replaced, o, s.time, {}, s.previous);
                } else
                    schedule({s.time, 0, Action::New, o, {}, s.previous});
                break;
            }
            case Action::Cancel:
                if (r.remaining && r.order.id == o.id) {
                    r = {};
                    response(Action::Cancelled, o, s.time);
                }
                break;
            case Action::Ack: {
                auto w = accepted(o, b.instrument(o.locate).symbol, s.time);
                if (!apply_report(w.view(), oms, on_fill))
                    return good_ = false;
                break;
            }
            case Action::Replaced: {
                auto w = replaced_report(o, s.previous, b.instrument(o.locate).symbol, s.time);
                if (!apply_report(w.view(), oms, on_fill))
                    return good_ = false;
                break;
            }
            case Action::Fill: {
                auto w = executed(s.fill, s.sequence + 1);
                if (!apply_report(w.view(), oms, on_fill))
                    return good_ = false;
                break;
            }
            case Action::Cancelled: {
                auto *record = oms.get(o.id);
                if (record && !terminal(record->state)) {
                    auto w = cancelled_report(o.id, record->order.qty - record->filled, s.time);
                    if (!apply_report(w.view(), oms, on_fill))
                        return good_ = false;
                }
                break;
            }
            }
            if (!good_)
                return false;
        }
        return true;
    }
    // Called before the historical event mutates the book. Cancels reduce queue ahead,
    // executions can fill us. New historical adds always join behind existing quotes.
    void on_event(const Event &e, const Book &b) noexcept {
        if (e.kind != Kind::Execute && e.kind != Kind::Cancel && e.kind != Kind::Delete &&
            e.kind != Kind::Replace)
            return;
        auto *n = b.order(e.id);
        if (!n)
            return;
        auto &level = b.level(n->level);
        auto removed = e.kind == Kind::Delete || e.kind == Kind::Replace ? n->qty : e.qty;
        // Walk our FIFO by ID, not storage slot order (IDs can wrap modulo the slot count).
        std::array<std::size_t, Oms::capacity> indexes{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < resting_.size(); ++i) {
            auto &r = resting_[i];
            if (r.remaining && r.order.locate == e.locate && r.order.side == level.side &&
                r.order.price == level.price)
                indexes[count++] = i;
        }
        std::sort(indexes.begin(), indexes.begin() + static_cast<std::ptrdiff_t>(count),
                  [&](auto a, auto c) { return resting_[a].order.id < resting_[c].order.id; });
        std::uint64_t already_filled = 0;
        for (std::size_t k = 0; k < count; ++k) {
            auto &r = resting_[indexes[k]];
            auto ahead = r.ahead;
            if (n->serial <= r.cutoff)
                r.ahead = ahead > removed ? ahead - removed : 0;
            if (e.kind == Kind::Execute && n->serial > r.cutoff && ahead == 0 &&
                removed > already_filled) {
                auto q = static_cast<Qty>(
                    std::min<std::uint64_t>(r.remaining, removed - already_filled));
                if (q) {
                    r.remaining -= q;
                    already_filled += q;
                    response(Action::Fill, r.order, e.time,
                             {r.order.id, q, level.price, e.time + latency_.network, true});
                }
            }
        }
    }
    template <class F> void each_resting(F &&f) const noexcept {
        for (auto &r : resting_)
            if (r.remaining)
                f(r.order, r.remaining, r.ahead);
    }
};
} // namespace hft
