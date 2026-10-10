#pragma once
#include "hft/gateway/encoder.hpp"
#include "hft/oms/oms.hpp"
namespace hft {
inline Wire accepted(const Order &o, Symbol symbol, Time time, bool dead = false) noexcept {
    auto enter = encode(o, symbol);
    Wire w;
    w.size = 66;
    w.bytes[0] = 'A';
    put_be(w.bytes, 1, time, 8);
    std::copy_n(enter.bytes.begin() + 1, 14, w.bytes.begin() + 9);
    std::copy_n(enter.bytes.begin() + 15, 26, w.bytes.begin() + 23);
    put_be(w.bytes, 49, o.id, 8);
    w.bytes[57] = 'P';
    w.bytes[58] = 'N';
    put_be(w.bytes, 59, 0, 4);
    w.bytes[63] = 'N';
    w.bytes[64] = dead ? 'D' : 'L';
    w.bytes[65] = ' ';
    return w;
}
inline Wire replaced_report(const Order &o, OrderId previous, Symbol symbol, Time time) noexcept {
    auto w = accepted(o, symbol, time, o.qty == 0);
    w.size = 80;
    w.bytes[0] = 'U';
    token(w.bytes, 65, previous);
    w.bytes[79] = ' ';
    return w;
}
inline Wire executed(const Fill &f, std::uint64_t match) noexcept {
    Wire w;
    w.size = 40;
    w.bytes[0] = 'E';
    put_be(w.bytes, 1, f.time, 8);
    token(w.bytes, 9, f.id);
    put_be(w.bytes, 23, f.qty, 4);
    put_be(w.bytes, 27, static_cast<std::uint64_t>(f.price), 4);
    w.bytes[31] = f.maker ? 'A' : 'R';
    put_be(w.bytes, 32, match, 8);
    return w;
}
inline Wire cancelled_report(OrderId id, Qty qty, Time time, char reason = 'U') noexcept {
    Wire w;
    w.size = 28;
    w.bytes[0] = 'C';
    put_be(w.bytes, 1, time, 8);
    token(w.bytes, 9, id);
    put_be(w.bytes, 23, qty, 4);
    w.bytes[27] = static_cast<unsigned char>(reason);
    return w;
}
inline Wire rejected_report(OrderId id, Time time, char reason = 'X') noexcept {
    Wire w;
    w.size = 24;
    w.bytes[0] = 'J';
    put_be(w.bytes, 1, time, 8);
    token(w.bytes, 9, id);
    w.bytes[23] = static_cast<unsigned char>(reason);
    return w;
}
// Sequence replay suppression is owned by SoupSession, which resumes at next expected.
// Reject unsupported response types rather than mutate state on an unrecognized layout.
template <class OnFill>
bool apply_report(std::span<const unsigned char> b, Oms &oms, OnFill &&on_fill) noexcept {
    if (b.empty())
        return false;
    if (b[0] == 'S')
        return b.size() == 10;
    if (b.size() < 23)
        return false;
    auto id = read_token(b, 9);
    auto *r = oms.get(id);
    if (!id || !r)
        return false;
    if (b[0] == 'A') {
        if (b.size() != 66 || be(b, 24, 4) != r->order.qty ||
            be(b, 36, 4) != static_cast<std::uint64_t>(r->order.price) ||
            (b[64] != 'L' && b[64] != 'D'))
            return false;
        if (!oms.ack(id))
            return terminal(r->state);
        if (b[64] == 'D')
            oms.cancel(id);
        return true;
    }
    if (b[0] == 'U') {
        if (b.size() != 80 || r->state != State::PendingAck || read_token(b, 65) != r->replaces ||
            !r->replaces)
            return false;
        auto *old = oms.get(r->replaces);
        auto q = static_cast<Qty>(be(b, 24, 4));
        if (!old || q > r->order.qty ||
            be(b, 36, 4) != static_cast<std::uint64_t>(r->order.price) ||
            (b[64] != 'L' && b[64] != 'D'))
            return false;
        if (!terminal(old->state))
            oms.cancel(old->order.id);
        r->order.qty = q;
        if (!q || b[64] == 'D')
            return oms.cancel(id);
        return oms.ack(id);
    }
    if (b[0] == 'E') {
        if (b.size() != 40)
            return false;
        Fill f{id, static_cast<Qty>(be(b, 23, 4)), static_cast<Price>(be(b, 27, 4)), be(b, 1, 8),
               b[31] == 'A'};
        if (!oms.fill(f))
            return false;
        on_fill(f);
        return true;
    }
    if (b[0] == 'C') {
        if (b.size() != 28 || terminal(r->state))
            return false;
        auto decrement = be(b, 23, 4);
        auto remaining = r->order.qty - r->filled;
        if (!decrement || decrement > remaining)
            return false;
        if (decrement == remaining)
            return oms.cancel(id);
        r->order.qty -= static_cast<Qty>(decrement);
        r->cancel_pending = false;
        return true;
    }
    if (b[0] == 'J')
        return b.size() == 24 && oms.reject(id);
    return false;
}
} // namespace hft
