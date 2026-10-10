#pragma once
#include "hft/core/index.hpp"
#include "hft/core/pool.hpp"
#include <algorithm>
#include <vector>
namespace hft {
// Intrusive FIFO orders. Sparse deterministic treaps support the full exchange price range.
// Best prices cached per instrument. Shared pools avoid per-symbol overprovisioning.
class Book {
  public:
    struct Node {
        OrderId id{};
        Qty qty{};
        std::uint32_t level{}, prev{}, next{};
        std::uint64_t serial{};
    };
    struct Level {
        Price price{};
        std::uint64_t qty{}, consumed{};
        Locate locate{};
        Side side{};
        std::uint32_t left{}, right{}, priority{}, head{}, tail{};
    };
    struct Instrument {
        std::array<std::uint32_t, 2> root{}, touch{};
        Symbol symbol{};
        Price last_trade{};
        char status = 'H';
        bool directory = false;
    };

  private:
    Pool<Node> orders_;
    Pool<Level> levels_;
    Index ids_;
    std::vector<Instrument> instruments_;
    std::size_t peak_ = 0;
    std::uint64_t serial_ = 0;
    static constexpr std::size_t dense_size = 4096;
    std::array<std::array<std::uint32_t, dense_size>, 2> dense_{};
    Locate dense_locate_;
    Price dense_base_ = 0;
    bool dense_enabled_, dense_initialized_ = false;
    std::uint32_t rotate_left(std::uint32_t r) noexcept {
        auto x = levels_[r].right;
        levels_[r].right = levels_[x].left;
        levels_[x].left = r;
        return x;
    }
    std::uint32_t rotate_right(std::uint32_t r) noexcept {
        auto x = levels_[r].left;
        levels_[r].left = levels_[x].right;
        levels_[x].right = r;
        return x;
    }
    std::uint32_t insert_level(std::uint32_t r, std::uint32_t n) noexcept {
        if (!r)
            return n;
        auto &l = levels_[r];
        if (levels_[n].price < l.price) {
            l.left = insert_level(l.left, n);
            if (levels_[l.left].priority < l.priority)
                r = rotate_right(r);
        } else {
            l.right = insert_level(l.right, n);
            if (levels_[l.right].priority < l.priority)
                r = rotate_left(r);
        }
        return r;
    }
    std::uint32_t merge(std::uint32_t a, std::uint32_t b) noexcept {
        if (!a)
            return b;
        if (!b)
            return a;
        if (levels_[a].priority < levels_[b].priority) {
            levels_[a].right = merge(levels_[a].right, b);
            return a;
        }
        levels_[b].left = merge(a, levels_[b].left);
        return b;
    }
    std::uint32_t erase_level(std::uint32_t r, Price price) noexcept {
        auto &l = levels_[r];
        if (price == l.price)
            return merge(l.left, l.right);
        if (price < l.price)
            l.left = erase_level(l.left, price);
        else
            l.right = erase_level(l.right, price);
        return r;
    }
    std::uint32_t extreme(std::uint32_t r, Side s) const noexcept {
        if (!r)
            return 0;
        while (true) {
            auto n = s == Side::Buy ? levels_[r].right : levels_[r].left;
            if (!n)
                return r;
            r = n;
        }
    }
    std::uint32_t locate_level(Locate loc, Side s, Price p) const noexcept {
        if (dense_enabled_ && dense_initialized_ && loc == dense_locate_ && p >= dense_base_ &&
            p < dense_base_ + static_cast<Price>(dense_size))
            return dense_[side_index(s)][static_cast<std::size_t>(p - dense_base_)];
        auto r = instruments_[loc].root[side_index(s)];
        while (r) {
            auto &l = levels_[r];
            if (l.price == p)
                return r;
            r = p < l.price ? l.left : l.right;
        }
        return 0;
    }
    void unlink(std::uint32_t i) noexcept {
        auto n = orders_[i];
        auto &l = levels_[n.level];
        if (n.prev)
            orders_[n.prev].next = n.next;
        else
            l.head = n.next;
        if (n.next)
            orders_[n.next].prev = n.prev;
        else
            l.tail = n.prev;
        l.qty -= n.qty;
        l.consumed = l.consumed > n.qty ? l.consumed - n.qty : 0;
        ids_.erase(n.id);
        orders_.release(i);
        if (!l.head) {
            if (dense_enabled_ && dense_initialized_ && l.locate == dense_locate_ &&
                l.price >= dense_base_ && l.price < dense_base_ + static_cast<Price>(dense_size))
                dense_[side_index(l.side)][static_cast<std::size_t>(l.price - dense_base_)] = 0;
            auto &inst = instruments_[l.locate];
            auto side = side_index(l.side);
            inst.root[side] = erase_level(inst.root[side], l.price);
            inst.touch[side] = extreme(inst.root[side], l.side);
            levels_.release(n.level);
        }
    }
    bool add(const Event &e) noexcept {
        if (ids_.get(e.id) || !e.id || !e.qty || e.price <= 0 || e.price > max_price)
            return false;
        auto i = orders_.acquire();
        if (!i)
            return false;
        if (dense_enabled_ && !dense_initialized_ && e.locate == dense_locate_) {
            dense_base_ = std::max<Price>(1, e.price - static_cast<Price>(dense_size / 2));
            dense_initialized_ = true;
        }
        auto l = locate_level(e.locate, e.side, e.price);
        if (!l) {
            l = levels_.acquire();
            if (!l) {
                orders_.release(i);
                return false;
            }
            auto &v = levels_[l];
            v.price = e.price;
            v.locate = e.locate;
            v.side = e.side;
            if (dense_enabled_ && e.locate == dense_locate_ && e.price >= dense_base_ &&
                e.price < dense_base_ + static_cast<Price>(dense_size))
                dense_[side_index(e.side)][static_cast<std::size_t>(e.price - dense_base_)] = l;
            v.priority = static_cast<std::uint32_t>(hash64(static_cast<std::uint64_t>(e.price) ^
                                                           (std::uint64_t(e.locate) << 32) ^
                                                           side_index(e.side)));
            auto &inst = instruments_[e.locate];
            auto s = side_index(e.side);
            inst.root[s] = insert_level(inst.root[s], l);
            auto t = inst.touch[s];
            if (!t ||
                (e.side == Side::Buy ? e.price > levels_[t].price : e.price < levels_[t].price))
                inst.touch[s] = l;
        }
        auto &v = levels_[l];
        orders_[i] = {e.id, e.qty, l, v.tail, 0, ++serial_};
        if (v.tail)
            orders_[v.tail].next = i;
        else
            v.head = i;
        v.tail = i;
        v.qty += e.qty;
        ids_.insert(e.id, i);
        peak_ = std::max(peak_, orders_.used());
        return true;
    }

  public:
    explicit Book(std::uint32_t orders = 262144, std::uint32_t levels = 65536,
                  Locate dense_locate = 1, bool dense_enabled = true)
        : orders_(orders), levels_(levels), ids_(orders), instruments_(65536),
          dense_locate_(dense_locate), dense_enabled_(dense_enabled) {}
    Book(const Book &) = delete;
    Book &operator=(const Book &) = delete;
    bool apply(const Event &e) noexcept {
        auto &inst = instruments_[e.locate];
        if (e.kind == Kind::Directory) {
            if (inst.directory && inst.symbol != e.symbol)
                return false;
            inst.symbol = e.symbol;
            inst.directory = true;
            return true;
        }
        if (e.kind == Kind::Status) {
            inst.status = e.code;
            return true;
        }
        if (e.kind == Kind::Trade) {
            if (e.printable)
                inst.last_trade = e.price;
            return true;
        }
        if (e.kind == Kind::Add) {
            if (!inst.directory) {
                inst.symbol = e.symbol;
            }
            return add(e);
        }
        if (e.kind != Kind::Cancel && e.kind != Kind::Execute && e.kind != Kind::Delete &&
            e.kind != Kind::Replace)
            return true;
        auto i = ids_.get(e.id);
        if (!i || levels_[orders_[i].level].locate != e.locate)
            return false;
        auto &n = orders_[i];
        auto &l = levels_[n.level];
        if (e.kind == Kind::Replace) {
            if (ids_.get(e.replacement) || !e.replacement || !e.qty || e.price <= 0 ||
                e.price > max_price)
                return false; // ensure capacity before destructive mutation
            if (e.price != l.price && !locate_level(e.locate, l.side, e.price) &&
                levels_.used() == levels_.capacity() && l.head != l.tail)
                return false;
            Event a = e;
            a.kind = Kind::Add;
            a.id = e.replacement;
            a.side = l.side;
            unlink(i);
            return add(a);
        }
        if (e.kind == Kind::Delete) {
            unlink(i);
            return true;
        }
        if (!e.qty || e.qty > n.qty)
            return false;
        if (e.kind == Kind::Execute && e.printable)
            inst.last_trade = e.price ? e.price : l.price;
        // Historical removals absorb already simulated consumed liquidity first.
        if (e.qty == n.qty)
            unlink(i);
        else {
            l.consumed = l.consumed > e.qty ? l.consumed - e.qty : 0;
            n.qty -= e.qty;
            l.qty -= e.qty;
        }
        return true;
    }
    const Node *order(OrderId id) const noexcept {
        auto i = ids_.get(id);
        return i ? &orders_[i] : nullptr;
    }
    const Level &level(std::uint32_t i) const noexcept {
        return levels_[i];
    }
    const Instrument &instrument(Locate l) const noexcept {
        return instruments_[l];
    }
    Price best(Side s, Locate loc = 1) const noexcept {
        auto t = instruments_[loc].touch[side_index(s)];
        return t ? levels_[t].price : 0;
    }
    std::uint64_t touch_qty(Side s, Locate loc = 1) const noexcept {
        auto i = instruments_[loc].touch[side_index(s)];
        return i ? levels_[i].qty : 0;
    }
    double imbalance(Locate loc = 1) const noexcept {
        auto x = double(touch_qty(Side::Buy, loc)), y = double(touch_qty(Side::Sell, loc));
        return x + y ? (x - y) / (x + y) : 0;
    }
    template <class F>
    void each_level(Locate loc, Side side, std::size_t limit, F &&f) const noexcept {
        auto p = best(side, loc);
        while (p && limit--) {
            f(p, depth(side, p, loc));
            p = next_price(loc, side, p);
        }
    }
    std::uint64_t depth(Side s, Price p, Locate loc = 1) const noexcept {
        auto l = locate_level(loc, s, p);
        return l ? levels_[l].qty : 0;
    }
    std::uint64_t available(Side s, Price p, Locate loc) const noexcept {
        auto l = locate_level(loc, s, p);
        return l ? levels_[l].qty - levels_[l].consumed : 0;
    }
    Qty consume(Locate loc, Side s, Price p, Qty requested) noexcept {
        auto i = locate_level(loc, s, p);
        if (!i)
            return 0;
        auto &l = levels_[i];
        auto q = static_cast<Qty>(std::min<std::uint64_t>(requested, l.qty - l.consumed));
        l.consumed += q;
        return q;
    }
    Price next_price(Locate loc, Side s, Price p) const noexcept {
        auto r = instruments_[loc].root[side_index(s)];
        Price result = 0;
        while (r) {
            auto &l = levels_[r];
            if (s == Side::Buy) {
                if (l.price < p) {
                    result = l.price;
                    r = l.right;
                } else
                    r = l.left;
            } else {
                if (l.price > p) {
                    result = l.price;
                    r = l.left;
                } else
                    r = l.right;
            }
        }
        return result;
    }
    std::uint64_t queue_ahead(OrderId id) const noexcept {
        auto i = ids_.get(id);
        if (!i)
            return 0;
        std::uint64_t q = 0;
        for (auto j = levels_[orders_[i].level].head; j && j != i; j = orders_[j].next)
            q += orders_[j].qty;
        return q;
    }
    template <class F> void each_order(F &&f) const {
        for (unsigned loc = 0; loc < 65536; ++loc)
            for (auto s : {Side::Buy, Side::Sell}) {
                auto price = best(s, static_cast<Locate>(loc));
                while (price) {
                    auto l = locate_level(static_cast<Locate>(loc), s, price);
                    for (auto i = levels_[l].head; i; i = orders_[i].next)
                        f(static_cast<Locate>(loc), s, price, orders_[i]);
                    price = next_price(static_cast<Locate>(loc), s, price);
                }
            }
    }
    bool huge_pages() noexcept {
        return orders_.huge_pages() && levels_.huge_pages();
    }
    std::uint64_t serial() const noexcept {
        return serial_;
    }
    std::size_t size() const noexcept {
        return orders_.used();
    }
    std::size_t peak() const noexcept {
        return peak_;
    }
    void reset() noexcept {
        orders_.reset();
        levels_.reset();
        ids_.reset();
        for (auto &i : instruments_)
            i = {};
        peak_ = 0;
        serial_ = 0;
        dense_initialized_ = false;
        for (auto &side : dense_)
            side.fill(0);
    }
};
} // namespace hft
