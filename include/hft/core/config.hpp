#pragma once
#include "hft/core/engine.hpp"
#include <charconv>
#include <fstream>
#include <string>
namespace hft {
// Cold startup parser. Reject unknown keys and out-of-range values instead of partial defaults.
inline bool load_config(const char *path, Config &config) {
    std::ifstream input(path);
    if (!input)
        return false;
    auto candidate = config;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        auto equal = line.find('=');
        if (equal == std::string::npos)
            return false;
        auto key = std::string_view(line).substr(0, equal),
             text = std::string_view(line).substr(equal + 1);
        std::uint64_t n = 0;
        auto r = std::from_chars(text.data(), text.data() + text.size(), n);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
            return false;
        if (key == "max_order_size" && n > 0 && n < 1000000)
            candidate.limits.max_size = static_cast<Qty>(n);
        else if (key == "max_position" && n > 0 && n <= 100000000)
            candidate.limits.max_position = static_cast<std::int64_t>(n);
        else if (key == "max_notional_ticks" && n > 0 && n <= INT64_MAX)
            candidate.limits.max_notional = static_cast<std::int64_t>(n);
        else if (key == "price_collar_ticks" && n <= max_price)
            candidate.limits.collar = static_cast<Price>(n);
        else if (key == "orders_per_second" && n > 0 && n <= UINT32_MAX)
            candidate.limits.orders_per_second = static_cast<std::uint32_t>(n);
        else if (key == "order_capacity" && n > 0 && n <= 100000000)
            candidate.order_capacity = static_cast<std::uint32_t>(n);
        else if (key == "level_capacity" && n > 0 && n <= 10000000)
            candidate.level_capacity = static_cast<std::uint32_t>(n);
        else if (key == "locate" && n < 65536)
            candidate.target = static_cast<Locate>(n);
        else if (key == "network_ns" && n <= 1000000000)
            candidate.latency.network = n;
        else if (key == "exchange_ns" && n <= 1000000000)
            candidate.latency.exchange = n;
        else if (key == "quote_ns")
            candidate.quote_interval = n;
        else if (key == "strategy_size" && n > 0 && n < 1000000)
            candidate.strategy_size = static_cast<Qty>(n);
        else
            return false;
    }
    if (!input.eof())
        return false;
    config = candidate;
    return true;
}
} // namespace hft
