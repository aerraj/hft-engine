#pragma once
#include "hft/book/book.hpp"
#include "hft/feed/replay.hpp"
#include <fstream>
namespace hft {
// Cold-path snapshot format: 8-byte magic + next Mold sequence, followed by ITCH records.
// Snapshot reconstructs FIFO in order, not simulated liquidity or OMS state.
inline bool save_snapshot(const char *path, const Book &book, std::uint64_t next) {
    std::ofstream out(path, std::ios::binary);
    std::array<unsigned char, 16> header{'H', 'F', 'T', 'S', 'N', 'A', 'P', '1'};
    put_be(header, 8, next, 8);
    out.write(reinterpret_cast<const char *>(header.data()), header.size());
    auto write = [&](std::span<const unsigned char> b) {
        std::array<unsigned char, 2> len{};
        put_be(len, 0, b.size(), 2);
        out.write(reinterpret_cast<const char *>(len.data()), 2);
        out.write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
    };
    for (unsigned i = 0; i < 65536; ++i) {
        auto &inst = book.instrument(static_cast<Locate>(i));
        if (inst.directory) {
            std::array<unsigned char, 39> r{};
            r[0] = 'R';
            put_be(r, 1, i, 2);
            std::copy(inst.symbol.begin(), inst.symbol.end(), r.begin() + 11);
            write(r);
        }
        if (inst.directory || book.best(Side::Buy, static_cast<Locate>(i)) ||
            book.best(Side::Sell, static_cast<Locate>(i))) {
            std::array<unsigned char, 25> h{};
            h[0] = 'H';
            put_be(h, 1, i, 2);
            h[19] = static_cast<unsigned char>(inst.status);
            write(h);
        }
    }
    book.each_order([&](Locate loc, Side side, Price price, const Book::Node &n) {
        std::array<unsigned char, 36> a{};
        a[0] = 'A';
        put_be(a, 1, loc, 2);
        put_be(a, 11, n.id, 8);
        a[19] = side == Side::Buy ? 'B' : 'S';
        put_be(a, 20, n.qty, 4);
        auto sym = book.instrument(loc).symbol;
        std::copy(sym.begin(), sym.end(), a.begin() + 24);
        put_be(a, 32, static_cast<std::uint64_t>(price), 4);
        write(a);
    });
    return bool(out);
}
inline bool load_snapshot(const char *path, Book &book, Sequence &sequence) {
    sequence.invalidate();
    std::ifstream in(path, std::ios::binary);
    std::array<unsigned char, 16> h{};
    in.read(reinterpret_cast<char *>(h.data()), 16);
    constexpr std::array<unsigned char, 8> magic{'H', 'F', 'T', 'S', 'N', 'A', 'P', '1'};
    if (in.gcount() != 16 || !std::equal(magic.begin(), magic.end(), h.begin()) || !be(h, 8, 8))
        return false;
    book.reset();
    std::array<unsigned char, 65535> buf{};
    while (true) {
        std::array<unsigned char, 2> len{};
        in.read(reinterpret_cast<char *>(len.data()), 2);
        if (in.gcount() == 0 && in.eof())
            break;
        if (in.gcount() != 2)
            return false;
        auto n = be(len, 0, 2);
        if (!n)
            return false;
        in.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(n));
        if (static_cast<std::uint64_t>(in.gcount()) != n)
            return false;
        Event e;
        if (!parse({buf.data(), static_cast<std::size_t>(n)}, e) ||
            (e.kind != Kind::Add && e.kind != Kind::Directory && e.kind != Kind::Status) ||
            !book.apply(e))
            return false;
    }
    sequence.recover(be(h, 8, 8));
    return true;
}
} // namespace hft
