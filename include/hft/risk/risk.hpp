#pragma once
#include "hft/oms/oms.hpp"
#include <algorithm>
#include <atomic>
#include <vector>
namespace hft {
struct Limits {
    Qty max_size = 100;
    std::int64_t max_position = 1000;
    std::int64_t max_notional = 100'000'000;
    Price collar = 1000;
    std::uint32_t orders_per_second = 10000;
};
enum class RiskResult : std::uint8_t {
    Pass,
    Killed,
    Size,
    Position,
    Notional,
    Collar,
    Rate,
    SelfTrade,
    Halted
};
class Risk {
    Limits limits_;
    Time window_ = 0;
    std::uint32_t count_ = 0;
    bool killed_ = false;

  public:
    explicit Risk(Limits limits = {}) : limits_(limits) {}
    void kill() noexcept {
        killed_ = true;
    }
    bool killed() const noexcept {
        return killed_;
    }
    // Limits change only at event boundaries; callers marshal control requests via SPSC.
    bool update(Limits l) noexcept {
        if (!l.max_size || l.max_size >= 1000000 || l.max_position <= 0 ||
            l.max_position > 100000000 || l.max_notional <= 0 || l.collar < 0 ||
            !l.orders_per_second)
            return false;
        limits_ = l;
        return true;
    }
    const Limits &limits() const noexcept {
        return limits_;
    }
    RiskResult check(const Order &o, const Oms &oms, Price reference, Time now,
                     bool trading = true) noexcept {
        if (killed_)
            return RiskResult::Killed;
        if (!trading)
            return RiskResult::Halted;
        if (!o.qty || o.qty > limits_.max_size || o.price <= 0 || o.price > max_price)
            return RiskResult::Size;
        auto pos = oms.account(o.locate).position;
        auto buy = oms.pending(o.locate, Side::Buy), sell = oms.pending(o.locate, Side::Sell);
        if (pos + static_cast<std::int64_t>(buy) + (o.side == Side::Buy ? o.qty : 0) >
                limits_.max_position ||
            pos - static_cast<std::int64_t>(sell) - (o.side == Side::Sell ? o.qty : 0) <
                -limits_.max_position)
            return RiskResult::Position;
        std::int64_t open = 0;
        oms.each([&](const Oms::Record &r) {
            if (r.order.locate == o.locate)
                open += r.order.price * std::int64_t(r.order.qty - r.filled);
        });
        auto abspos = pos < 0 ? -pos : pos;
        if (abspos * o.price + open + o.price * std::int64_t(o.qty) > limits_.max_notional)
            return RiskResult::Notional;
        if (!reference || o.price < reference - limits_.collar ||
            o.price > reference + limits_.collar)
            return RiskResult::Collar;
        bool crossing = false;
        oms.each([&](const Oms::Record &r) {
            if (r.order.locate == o.locate && r.order.side != o.side &&
                (o.side == Side::Buy ? o.price >= r.order.price : o.price <= r.order.price))
                crossing = true;
        });
        if (crossing)
            return RiskResult::SelfTrade;
        auto second = now / 1'000'000'000;
        if (second != window_) {
            window_ = second;
            count_ = 0;
        }
        if (count_ >= limits_.orders_per_second)
            return RiskResult::Rate;
        return RiskResult::Pass;
    }
    void committed() noexcept {
        ++count_;
    }
};
} // namespace hft
