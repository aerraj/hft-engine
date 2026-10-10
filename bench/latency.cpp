#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include "hft/core/engine.hpp"
#include "hft/log/histogram.hpp"
using Clock=std::chrono::steady_clock;
volatile std::uint64_t sink=0;
int main(){constexpr int N=100000;std::vector<double> times;times.reserve(N);hft::Engine<hft::Taker> e;std::array<unsigned char,36>a{};a[0]='A';a[19]='B';a[23]=1;a[32]=0;a[33]=1;a[34]=134;a[35]=160; // 100000
 std::array<unsigned char,23>x{};x[0]='X';x[22]=1;
 for(int i=0;i<N+10000;++i){auto id=std::uint64_t{1};for(int k=0;k<8;++k)a[18-k]=x[18-k]=static_cast<unsigned char>(id>>(8*k));auto t=Clock::now();bool ok=e.process(a);auto end=Clock::now();if(!ok)return 1;sink=e.events;e.process(x);if(i>=10000)times.push_back(std::chrono::duration<double,std::nano>(end-t).count());}
 std::array<unsigned,5> bins{};for(auto v:times)++bins[v<500?0:v<1000?1:v<2000?2:v<5000?3:4];std::cerr<<"bucket_ns,count\n0-499,"<<bins[0]<<"\n500-999,"<<bins[1]<<"\n1000-1999,"<<bins[2]<<"\n2000-4999,"<<bins[3]<<"\n5000+,"<<bins[4]<<"\n";std::sort(times.begin(),times.end());std::cout<<"{\"stage\":\"parse_book_decision_risk_no_order\",\"samples\":"<<N<<",\"p50_ns\":"<<hft::percentile(times,.5)<<",\"p99_ns\":"<<hft::percentile(times,.99)<<",\"p999_ns\":"<<hft::percentile(times,.999)<<",\"max_ns\":"<<times.back()<<"}\n";}
