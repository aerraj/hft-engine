#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace hft {
using Price = std::int64_t; // exchange fixed point, $1 = 10,000 ticks
using Qty = std::uint32_t;
using OrderId = std::uint64_t;
using Time = std::uint64_t;
using Locate = std::uint16_t;
using Symbol = std::array<char, 8>;
constexpr Price max_price = 2'000'000'000;
enum class Side : std::uint8_t { Buy, Sell };
constexpr unsigned side_index(Side s) noexcept {
    return s == Side::Buy ? 0 : 1;
}
constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}
enum class Kind : std::uint8_t {
    Ignore,
    Directory,
    Status,
    System,
    Add,
    Cancel,
    Delete,
    Replace,
    Execute,
    Trade,
    BrokenTrade
};
struct Event {
    Kind kind = Kind::Ignore;
    Locate locate{};
    Time time{};
    OrderId id{}, replacement{}, match{};
    Side side{};
    Price price{};
    Qty qty{};
    Symbol symbol{};
    char code{};
    bool printable = true;
};
struct Order {
    OrderId id{};
    Locate locate{};
    Side side{};
    Price price{};
    Qty qty{};
    bool ioc = false;
};
struct Fill {
    OrderId id{};
    Qty qty{};
    Price price{};
    Time time{};
    bool maker = false;
};
struct Intent {
    std::array<Order, 2> orders{};
    unsigned count{};
};
inline std::uint64_t hash64(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
} // namespace hft
