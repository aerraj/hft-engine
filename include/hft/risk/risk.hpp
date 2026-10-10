#pragma once
#include <cstdlib>
#include "hft/core/types.hpp"
namespace hft {
struct Risk {Qty max_size=100;std::int64_t max_position=100;Price max_price=200000;bool killed=false;
 bool check(const Order&o,std::int64_t position)const noexcept {auto p=position+(o.side==Side::Buy?static_cast<std::int64_t>(o.qty):-static_cast<std::int64_t>(o.qty));return !killed&&o.qty>0&&o.qty<=max_size&&o.price>0&&o.price<=max_price&&p>=-max_position&&p<=max_position;}
};
}
