#pragma once
#include "hft/core/types.hpp"
#include <algorithm>
namespace hft {
inline std::uint64_t be(std::span<const unsigned char> b, std::size_t at, std::size_t n) noexcept {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i)
        v = (v << 8) | b[at + i];
    return v;
}
inline void put_be(std::span<unsigned char> b, std::size_t at, std::uint64_t v,
                   std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i)
        b[at + n - 1 - i] = static_cast<unsigned char>(v >> (8 * i));
}
inline std::size_t itch_size(unsigned char t) noexcept {
    switch (t) {
    case 'S':
        return 12;
    case 'R':
        return 39;
    case 'H':
        return 25;
    case 'Y':
        return 20;
    case 'L':
        return 26;
    case 'V':
        return 35;
    case 'W':
        return 12;
    case 'K':
        return 28;
    case 'J':
        return 35;
    case 'h':
        return 21;
    case 'A':
        return 36;
    case 'F':
        return 40;
    case 'E':
        return 31;
    case 'C':
        return 36;
    case 'X':
        return 23;
    case 'D':
        return 19;
    case 'U':
        return 35;
    case 'P':
        return 44;
    case 'Q':
        return 40;
    case 'B':
        return 19;
    case 'I':
        return 50;
    case 'N':
        return 20;
    default:
        return 0;
    }
}
inline bool parse(std::span<const unsigned char> b, Event &e) noexcept {
    if (b.empty() || !itch_size(b[0]) || b.size() != itch_size(b[0]))
        return false;
    e = {};
    e.locate = static_cast<Locate>(be(b, 1, 2));
    e.time = be(b, 5, 6);
    if (e.time >= 86'400'000'000'000ULL)
        return false;
    auto symbol = [&](std::size_t at) {
        std::copy_n(b.begin() + static_cast<std::ptrdiff_t>(at), 8, e.symbol.begin());
    };
    switch (b[0]) {
    case 'S':
        e.kind = Kind::System;
        e.code = static_cast<char>(b[11]);
        break;
    case 'R':
        e.kind = Kind::Directory;
        symbol(11);
        break;
    case 'H':
        e.kind = Kind::Status;
        e.code = static_cast<char>(b[19]);
        symbol(11);
        break;
    case 'W':
        e.kind = Kind::System;
        e.code = '!';
        break;
    case 'h':
        e.kind = Kind::Status;
        e.code = b[20] == '0' ? 'T' : 'H';
        symbol(11);
        break;
    case 'A':
    case 'F':
        e.kind = Kind::Add;
        e.id = be(b, 11, 8);
        if (b[19] != 'B' && b[19] != 'S')
            return false;
        e.side = b[19] == 'B' ? Side::Buy : Side::Sell;
        e.qty = static_cast<Qty>(be(b, 20, 4));
        symbol(24);
        e.price = static_cast<Price>(be(b, 32, 4));
        if (!e.id || !e.qty || e.price <= 0 || e.price > max_price)
            return false;
        break;
    case 'X':
    case 'E':
    case 'C':
        e.kind = b[0] == 'X' ? Kind::Cancel : Kind::Execute;
        e.id = be(b, 11, 8);
        e.qty = static_cast<Qty>(be(b, 19, 4));
        if (!e.id || !e.qty)
            return false;
        if (b[0] != 'X')
            e.match = be(b, 23, 8);
        if (b[0] == 'C') {
            e.printable = b[31] == 'Y';
            e.price = static_cast<Price>(be(b, 32, 4));
            if (e.price > max_price)
                return false;
        }
        break;
    case 'D':
        e.kind = Kind::Delete;
        e.id = be(b, 11, 8);
        if (!e.id)
            return false;
        break;
    case 'U':
        e.kind = Kind::Replace;
        e.id = be(b, 11, 8);
        e.replacement = be(b, 19, 8);
        e.qty = static_cast<Qty>(be(b, 27, 4));
        e.price = static_cast<Price>(be(b, 31, 4));
        if (!e.id || !e.replacement || !e.qty || e.price <= 0 || e.price > max_price)
            return false;
        break;
    case 'P':
        e.kind = Kind::Trade;
        e.qty = static_cast<Qty>(be(b, 20, 4));
        symbol(24);
        e.price = static_cast<Price>(be(b, 32, 4));
        e.match = be(b, 36, 8);
        if (!e.qty || e.price <= 0 || e.price > max_price)
            return false;
        break;
    case 'Q':
        e.kind = Kind::Trade;
        symbol(19);
        e.price = static_cast<Price>(be(b, 27, 4));
        e.match = be(b, 31, 8);
        e.printable = false;
        break; // cross volume is 64-bit; not a displayed-book update
    case 'B':
        e.kind = Kind::BrokenTrade;
        e.match = be(b, 11, 8);
        break;
    default:
        break; // administrative/NOII/RPI messages validated and counted, no displayed-book mutation
    }
    return true;
}
class Sequence {
    std::uint64_t next_ = 1;
    bool gap_ = false;

  public:
    bool accept(std::uint64_t n) noexcept {
        if (gap_ || n != next_) {
            gap_ = true;
            return false;
        }
        ++next_;
        return true;
    }
    bool healthy() const noexcept {
        return !gap_;
    }
    std::uint64_t next() const noexcept {
        return next_;
    }
    void invalidate() noexcept {
        gap_ = true;
    }
    void recover(std::uint64_t n) noexcept {
        next_ = n;
        gap_ = false;
    }
};
// MoldUDP64: validate the entire datagram before calling the consumer, avoiding partial
// application.
template <class Consumer>
bool mold(std::span<const unsigned char> b, Sequence &seq, Consumer &&consume) noexcept {
    if (b.size() < 20)
        return false;
    auto first = be(b, 10, 8), count = be(b, 18, 2);
    if (count == 65535)
        return b.size() == 20;
    auto at = std::size_t{20};
    for (std::uint64_t i = 0; i < count; ++i) {
        if (at + 2 > b.size())
            return false;
        auto n = be(b, at, 2);
        at += 2;
        if (!n || at + n > b.size())
            return false;
        Event e;
        if (!parse(b.subspan(at, n), e))
            return false;
        at += n;
    }
    if (at != b.size())
        return false;
    if (first != seq.next()) {
        seq.accept(first);
        return false;
    }
    if (!seq.healthy())
        return false;
    at = 20;
    for (std::uint64_t i = 0; i < count; ++i) {
        auto n = be(b, at, 2);
        at += 2;
        if (!seq.accept(first + i) || !consume(b.subspan(at, n)))
            return false;
        at += n;
    }
    return true;
}
} // namespace hft
