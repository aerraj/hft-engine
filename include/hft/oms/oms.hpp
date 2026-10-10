#pragma once
#include "hft/core/types.hpp"
namespace hft {
enum class State {New,PendingAck,Live,PartiallyFilled,Filled,Cancelled,Rejected};
struct Oms {OrderId next=1;std::int64_t position=0,cash=0;Order assign(Order o)noexcept{o.id=next++;return o;}void fill(const Order&o)noexcept{auto q=static_cast<std::int64_t>(o.qty);if(o.side==Side::Buy){position+=q;cash-=o.price*q;}else{position-=q;cash+=o.price*q;}}std::int64_t equity(Price mark)const noexcept{return cash+position*mark;}};
}
