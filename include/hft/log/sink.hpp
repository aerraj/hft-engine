#pragma once
#include "hft/book/book.hpp"
#include "hft/log/logger.hpp"
#include "hft/oms/oms.hpp"
#include <bit>
namespace hft {
inline std::uint64_t bits(std::int64_t v) noexcept {
    return std::bit_cast<std::uint64_t>(v);
}
struct BinarySink {
    AsyncLogger &logger;
    bool wire(Time t, const Wire &w) noexcept {
        return logger.push({t, 'O', w});
    }
    bool fill(const Fill &f, const Oms &oms) noexcept {
        auto *r = oms.get(f.id);
        if (!r)
            return false;
        auto &a = oms.account(r->order.locate);
        Wire w;
        w.size = 48;
        put_be(w.bytes, 0, f.id, 8);
        put_be(w.bytes, 8, f.qty, 4);
        put_be(w.bytes, 12, bits(f.price), 8);
        w.bytes[20] = f.maker ? 1 : 0;
        w.bytes[21] = r->order.side == Side::Buy ? 'B' : 'S';
        put_be(w.bytes, 22, r->order.locate, 2);
        put_be(w.bytes, 24, bits(a.position), 8);
        put_be(w.bytes, 32, bits(a.cash), 8);
        put_be(w.bytes, 40, bits(a.realized), 8);
        return logger.push({f.time, 'F', w});
    }
    bool sample(Time t, const Book &b, const Oms &oms, Locate loc) noexcept {
        Wire w;
        w.size = 58;
        auto &a = oms.account(loc);
        put_be(w.bytes, 0, loc, 2);
        put_be(w.bytes, 2, bits(b.best(Side::Buy, loc)), 8);
        put_be(w.bytes, 10, bits(b.best(Side::Sell, loc)), 8);
        put_be(w.bytes, 18, bits(a.position), 8);
        put_be(w.bytes, 26, bits(a.cash), 8);
        put_be(w.bytes, 34, bits(a.realized), 8);
        put_be(w.bytes, 42, bits(a.basis), 8);
        put_be(w.bytes, 50, bits(b.instrument(loc).last_trade), 8);
        return logger.push({t, 'M', w});
    }
};
} // namespace hft
