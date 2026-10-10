#pragma once
#include <algorithm>
#include <vector>
namespace hft {inline double percentile(const std::vector<double>& sorted,double p){return sorted.empty()?0:sorted[static_cast<std::size_t>((sorted.size()-1)*p)];}}
