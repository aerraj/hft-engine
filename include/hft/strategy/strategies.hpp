#pragma once
#include "hft/book/book.hpp"
namespace hft {
struct Maker {template<class B> Order decide(const B&b,std::int64_t inventory)const noexcept {auto bid=b.best(Side::Buy),ask=b.best(Side::Sell);if(!bid||!ask||bid>=ask)return {};auto side=inventory>0?Side::Sell:Side::Buy;return {0,side,side==Side::Buy?bid:ask,1};}};
struct Taker {template<class B> Order decide(const B&b,std::int64_t)const noexcept {auto bid=b.best(Side::Buy),ask=b.best(Side::Sell);if(!bid||!ask||bid>=ask)return {};auto x=b.depth(Side::Buy,bid),y=b.depth(Side::Sell,ask);if(x>2*y)return {0,Side::Buy,ask,1};if(y>2*x)return {0,Side::Sell,bid,1};return {};}};
}
