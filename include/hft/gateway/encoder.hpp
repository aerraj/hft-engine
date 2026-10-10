#pragma once
#include <array>
#include "hft/core/types.hpp"
namespace hft {
// Private simulator wire format, deliberately not exchange-compatible OUCH.
using Wire=std::array<unsigned char,25>;
inline Wire encode(const Order&o)noexcept {Wire w{};w[0]='O';w[1]=o.side==Side::Buy?'B':'S';auto put=[&](std::size_t at,std::uint64_t v,std::size_t n){for(std::size_t i=0;i<n;++i)w[at+n-1-i]=static_cast<unsigned char>(v>>(8*i));};put(2,o.id,8);put(10,static_cast<std::uint64_t>(o.price),8);put(18,o.qty,4);return w;}
}
