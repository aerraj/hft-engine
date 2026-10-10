#pragma once
#include <cstdint>
namespace hft {
using Price=std::int64_t; using Qty=std::uint32_t; using OrderId=std::uint64_t;
enum class Side:std::uint8_t { Buy, Sell };
enum class Kind:std::uint8_t { Add, Cancel, Execute };
struct Event { Kind kind{}; OrderId id{}; Side side{}; Price price{}; Qty qty{}; };
struct Order { OrderId id{}; Side side{}; Price price{}; Qty qty{}; };
}
