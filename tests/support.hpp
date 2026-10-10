#pragma once
#include "hft/feed/parser.hpp"
#include <vector>
namespace test {
inline hft::Symbol symbol() {
    return {'T', 'E', 'S', 'T', ' ', ' ', ' ', ' '};
}
inline std::vector<unsigned char> packet(char type, hft::Time t = 0, hft::Locate loc = 1) {
    std::vector<unsigned char> b(hft::itch_size(static_cast<unsigned char>(type)));
    b[0] = static_cast<unsigned char>(type);
    hft::put_be(b, 1, loc, 2);
    hft::put_be(b, 5, t, 6);
    return b;
}
inline auto add(hft::OrderId id, hft::Side s, hft::Price price, hft::Qty qty, hft::Time t = 0,
                hft::Locate loc = 1) {
    auto b = packet('A', t, loc);
    hft::put_be(b, 11, id, 8);
    b[19] = s == hft::Side::Buy ? 'B' : 'S';
    hft::put_be(b, 20, qty, 4);
    auto sym = symbol();
    std::copy(sym.begin(), sym.end(), b.begin() + 24);
    hft::put_be(b, 32, static_cast<std::uint64_t>(price), 4);
    return b;
}
inline auto remove(char type, hft::OrderId id, hft::Qty qty, hft::Time t = 0, hft::Locate loc = 1) {
    auto b = packet(type, t, loc);
    hft::put_be(b, 11, id, 8);
    if (type != 'D')
        hft::put_be(b, 19, qty, 4);
    return b;
}
inline hft::Event event(hft::OrderId id, hft::Side s, hft::Price p, hft::Qty q,
                        hft::Locate loc = 1) {
    hft::Event e;
    e.kind = hft::Kind::Add;
    e.locate = loc;
    e.id = id;
    e.side = s;
    e.price = p;
    e.qty = q;
    e.symbol = symbol();
    return e;
}
inline hft::Event removal(hft::Kind kind, hft::OrderId id, hft::Qty q = 0, hft::Time t = 0,
                          hft::Locate loc = 1) {
    hft::Event e;
    e.kind = kind;
    e.locate = loc;
    e.id = id;
    e.qty = q;
    e.time = t;
    return e;
}
template <class E> void start(E &e) {
    auto s = packet('S', 0, 0);
    s[11] = 'Q';
    e.process(s);
    auto h = packet('H', 1);
    h[19] = 'T';
    e.process(h);
}
} // namespace test
