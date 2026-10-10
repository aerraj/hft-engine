#pragma once
#include "hft/feed/parser.hpp"
#include <algorithm>
#include <charconv>
namespace hft {
struct Wire {
    std::array<unsigned char, 128> bytes{};
    std::uint16_t size{};
    std::span<const unsigned char> view() const noexcept {
        return {bytes.data(), size};
    }
};
inline void token(std::span<unsigned char> out, std::size_t at, OrderId id) noexcept {
    std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(at), 14, '0');
    std::array<char, 20> s{};
    auto r = std::to_chars(s.data(), s.data() + s.size(), id, 36);
    auto n = static_cast<std::size_t>(r.ptr - s.data());
    std::copy_n(s.begin(), n, out.begin() + static_cast<std::ptrdiff_t>(at + 14 - n));
}
inline OrderId read_token(std::span<const unsigned char> b, std::size_t at) noexcept {
    OrderId id = 0;
    for (unsigned i = 0; i < 14; ++i) {
        auto c = b[at + i];
        unsigned v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : 36;
        if (v >= 36 || id > (UINT64_MAX - v) / 36)
            return 0;
        id = id * 36 + v;
    }
    return id;
}
inline Wire encode(const Order &o, Symbol symbol) noexcept {
    Wire w;
    w.size = 49;
    auto b = std::span(w.bytes);
    b[0] = 'O';
    token(b, 1, o.id);
    b[15] = o.side == Side::Buy ? 'B' : 'S';
    put_be(b, 16, o.qty, 4);
    std::copy(symbol.begin(), symbol.end(), b.begin() + 20);
    put_be(b, 28, static_cast<std::uint64_t>(o.price), 4);
    put_be(b, 32, o.ioc ? 0 : 99998, 4);
    std::fill_n(b.begin() + 36, 4, ' ');
    b[40] = 'Y';
    b[41] = 'P';
    b[42] = 'N';
    put_be(b, 43, 0, 4);
    b[47] = 'N';
    b[48] = 'N';
    return w;
}
inline Wire encode_cancel(OrderId id) noexcept {
    Wire w;
    w.size = 19;
    w.bytes[0] = 'X';
    token(w.bytes, 1, id);
    put_be(w.bytes, 15, 0, 4);
    return w;
}
inline Wire encode_replace(OrderId old, const Order &o) noexcept {
    Wire w;
    w.size = 47;
    auto b = std::span(w.bytes);
    b[0] = 'U';
    token(b, 1, old);
    token(b, 15, o.id);
    put_be(b, 29, o.qty, 4);
    put_be(b, 33, static_cast<std::uint64_t>(o.price), 4);
    put_be(b, 37, o.ioc ? 0 : 99998, 4);
    b[41] = 'Y';
    b[42] = 'N';
    put_be(b, 43, 0, 4);
    return w;
}
inline bool decode_order(std::span<const unsigned char> b, Order &o, Symbol &symbol) noexcept {
    if (b.size() != 49 || b[0] != 'O' || (b[15] != 'B' && b[15] != 'S'))
        return false;
    o = {read_token(b, 1),
         0,
         b[15] == 'B' ? Side::Buy : Side::Sell,
         static_cast<Price>(be(b, 28, 4)),
         static_cast<Qty>(be(b, 16, 4)),
         be(b, 32, 4) == 0};
    std::copy_n(b.begin() + 20, 8, symbol.begin());
    return o.id && o.qty && o.qty < 1000000 && o.price > 0 && o.price <= max_price;
}
} // namespace hft
