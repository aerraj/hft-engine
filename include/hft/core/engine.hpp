#pragma once
#include "hft/feed/parser.hpp"
#include "hft/strategy/strategies.hpp"
#include "hft/risk/risk.hpp"
#include "hft/sim/exchange.hpp"
#include "hft/gateway/encoder.hpp"
namespace hft {
template<class Strategy> struct Engine {Book<> book;Strategy strategy;Risk risk;Oms oms;Exchange exchange;Wire last{};std::uint64_t events=0,orders=0,fills=0;
 bool process(std::span<const unsigned char> packet)noexcept{Event e;if(!parse(packet,e)||!book.apply(e))return false;++events;auto o=strategy.decide(book,oms.position);if(risk.check(o,oms.position)){o=oms.assign(o);last=encode(o);++orders;if(exchange.submit(o,book,oms))++fills;}return true;}
};
}
