#pragma once
#include <array>
#include "hft/core/types.hpp"
namespace hft {
// Bounded single-instrument book. IDs use open addressing; deleted slots remain tombstones.
template<std::size_t Capacity=8192,std::size_t Levels=2048> class Book {
 struct Node {OrderId id{};Price price{};Qty qty{};Side side{};std::uint8_t state{};};
 std::array<Node,Capacity> nodes{};
 std::array<std::uint64_t,Levels> bids{},asks{};
 Price base;
 Node* find(OrderId id) noexcept {auto k=id%Capacity;for(std::size_t i=0;i<Capacity;++i){auto&n=nodes[(k+i)%Capacity];if(n.state==0)return nullptr;if(n.state==1&&n.id==id)return &n;}return nullptr;}
public:
 explicit Book(Price b=99000):base(b){}
 bool apply(const Event&e) noexcept {
 if(e.kind==Kind::Add){if(!e.qty||e.price<base||e.price>=base+static_cast<Price>(Levels)||find(e.id))return false;
 Node* slot=nullptr;auto k=e.id%Capacity;for(std::size_t i=0;i<Capacity;++i){auto&n=nodes[(k+i)%Capacity];if(n.state!=1){slot=&n;break;}}if(!slot)return false;
 *slot={e.id,e.price,e.qty,e.side,1};(e.side==Side::Buy?bids:asks)[e.price-base]+=e.qty;return true;}
 auto*n=find(e.id);if(!n||!e.qty||e.qty>n->qty)return false;
 (n->side==Side::Buy?bids:asks)[n->price-base]-=e.qty;n->qty-=e.qty;if(!n->qty)n->state=2;return true;
 }
 Price best(Side s)const noexcept {if(s==Side::Buy){for(std::size_t i=Levels;i-->0;)if(bids[i])return base+static_cast<Price>(i);}else{for(std::size_t i=0;i<Levels;++i)if(asks[i])return base+static_cast<Price>(i);}return 0;}
 std::uint64_t depth(Side s,Price p)const noexcept {if(p<base||p>=base+static_cast<Price>(Levels))return 0;return (s==Side::Buy?bids:asks)[p-base];}
};
}
