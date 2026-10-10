#pragma once
#include <span>
#include <cstddef>
#include "hft/core/types.hpp"
namespace hft {
// NASDAQ ITCH 5.0 A/X/E payloads; outer length prefix belongs to replay I/O.
inline std::uint64_t be(std::span<const unsigned char> b,std::size_t at,std::size_t n) noexcept {std::uint64_t v=0;for(std::size_t i=0;i<n;++i)v=(v<<8)|b[at+i];return v;}
inline bool parse(std::span<const unsigned char>b,Event& e) noexcept {
 if(b.empty())return false;
 if(b[0]=='A' && b.size()==36){if(b[19]!='B'&&b[19]!='S')return false;e={Kind::Add,be(b,11,8),b[19]=='B'?Side::Buy:Side::Sell,static_cast<Price>(be(b,32,4)),static_cast<Qty>(be(b,20,4))};return e.qty>0;}
 if((b[0]=='X'&&b.size()==23)||(b[0]=='E'&&b.size()==31)){e={b[0]=='X'?Kind::Cancel:Kind::Execute,be(b,11,8),Side::Buy,0,static_cast<Qty>(be(b,19,4))};return e.qty>0;}
 return false;
}
class Sequence {std::uint64_t next=1;bool gap=false;public:bool accept(std::uint64_t n) noexcept {if(gap||n!=next){gap=true;return false;}++next;return true;}bool healthy()const noexcept{return !gap;}void recover(std::uint64_t n)noexcept{next=n;gap=false;}};
}
