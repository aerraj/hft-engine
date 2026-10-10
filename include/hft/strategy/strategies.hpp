#pragma once
#include "hft/book/book.hpp"
#include <algorithm>
#include <concepts>
namespace hft {
struct Maker {
    Qty size = 10;
    Price half_spread = 1, skew_ticks = 1;
    std::int64_t inventory_scale = 100;
    double ema = 0;
    Intent on_book_update(const Book &b, Locate loc, std::int64_t inventory) noexcept {
        auto bid = b.best(Side::Buy, loc), ask = b.best(Side::Sell, loc);
        if (!bid || !ask || bid >= ask)
            return {};
        auto x = b.depth(Side::Buy, bid, loc), y = b.depth(Side::Sell, ask, loc);
        __extension__ using Wide = unsigned __int128;
        auto micro = bid + static_cast<Price>((static_cast<Wide>(ask - bid) * x) / (x + y));
        auto fair = micro - inventory * skew_ticks / std::max<std::int64_t>(1, inventory_scale);
        auto buy = std::min(bid, fair - half_spread), sell = std::max(ask, fair + half_spread);
        if (buy <= 0 || sell > max_price)
            return {};
        return {{{{0, loc, Side::Buy, buy, size, false}, {0, loc, Side::Sell, sell, size, false}}},
                2};
    }
    void on_trade(Price p, Qty) noexcept {
        ema = ema == 0 ? double(p) : ema + .05 * (double(p) - ema);
    }
    void on_fill(const Fill &) noexcept {}
    void on_timer(Time) noexcept {}
};
struct Taker {
    Qty size = 10;
    double threshold = .2, alpha = .2, imbalance = 0;
    Time cooldown = 100000, previous = 0;
    Intent on_book_update(const Book &b, Locate loc, std::int64_t) noexcept {
        auto bid = b.best(Side::Buy, loc), ask = b.best(Side::Sell, loc);
        if (!bid || !ask || bid >= ask)
            return {};
        auto x = double(b.depth(Side::Buy, bid, loc)), y = double(b.depth(Side::Sell, ask, loc));
        imbalance += alpha * ((x - y) / (x + y) - imbalance);
        if (imbalance > threshold)
            return {{{{0, loc, Side::Buy, ask, size, true}, {}}}, 1};
        if (imbalance < -threshold)
            return {{{{0, loc, Side::Sell, bid, size, true}, {}}}, 1};
        return {};
    }
    void on_trade(Price, Qty) noexcept {}
    void on_fill(const Fill &) noexcept {}
    void on_timer(Time) noexcept {}
};
template <class S>
concept Strategy = requires(S s, const Book &b, Locate loc, std::int64_t pos, Fill f, Time t) {
    { s.on_book_update(b, loc, pos) } -> std::same_as<Intent>;
    s.on_trade(Price{}, Qty{});
    s.on_fill(f);
    s.on_timer(t);
};
static_assert(Strategy<Maker> && Strategy<Taker>);
} // namespace hft
