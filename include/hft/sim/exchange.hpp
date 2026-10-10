#pragma once
#include "hft/oms/oms.hpp"
namespace hft {
// Immediate aggressive fills only. Passive quotes do not receive speculative fills.
struct Exchange {template<class B> bool submit(const Order&o,const B&b,Oms&oms)const noexcept {auto touch=b.best(o.side==Side::Buy?Side::Sell:Side::Buy);if(!touch)return false;if((o.side==Side::Buy&&o.price>=touch)||(o.side==Side::Sell&&o.price<=touch)){if(b.depth(o.side==Side::Buy?Side::Sell:Side::Buy,touch)<o.qty)return false;auto fill=o;fill.price=touch;oms.fill(fill);return true;}return false;}};
}
