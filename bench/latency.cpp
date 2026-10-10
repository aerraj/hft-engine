#include "../tests/support.hpp"
#include "hft/core/engine.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <sys/utsname.h>
namespace {
std::atomic<bool> audit = false;
std::atomic<std::uint64_t> allocations = 0;
} // namespace
void *operator new(std::size_t n) {
    if (audit.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    std::abort();
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete[](void *p) noexcept {
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    ::operator delete(p);
}
void *operator new(std::size_t n, std::align_val_t alignment) {
    if (audit.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    void *p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(alignment), n ? n : 1))
        std::abort();
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
int main(int argc, char **argv) {
    std::uint64_t samples = 100000;
    std::string prefix = "latency";
    bool pinned = false, dense = true;
    for (int i = 1; i < argc; ++i) {
        std::string_view key = argv[i];
        if (i + 1 >= argc)
            return 2;
        if (key == "--dense")
            dense = std::string_view(argv[++i]) != "0";
        else if (key == "--samples")
            samples = std::strtoull(argv[++i], nullptr, 10);
        else if (key == "--out")
            prefix = argv[++i];
        else if (key == "--core") {
            pinned = hft::pin_core(static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10)));
            if (!pinned)
                return 2;
        } else
            return 2;
    }
    if (!samples || samples > 100000000)
        return 2;
    auto clock = std::make_unique<hft::Clock>();
    auto metrics = std::make_unique<hft::Metrics>();
    hft::Config c;
    c.dense_index = dense;
    c.latency = {0, 0};
    c.quote_interval = 0;
    c.limits.max_position = 100000000;
    c.limits.max_notional = INT64_MAX;
    c.limits.orders_per_second = UINT32_MAX;
    auto engine = std::make_unique<hft::Engine<hft::Maker>>(c, clock.get(), nullptr);
    test::start(*engine);
    engine->process(test::add(1, hft::Side::Buy, 100000, 1000, 2));
    engine->process(test::add(2, hft::Side::Sell, 100004, 1000, 3));
    // Changing touch forces cancel/new orders on alternate iterations, avoiding a no-order
    // benchmark.
    auto add = test::add(3, hft::Side::Buy, 100001, 1000, 4), del = test::remove('D', 3, 0, 5);
    hft::NullSink sink;
    auto cycle = [&](std::uint64_t i) {
        auto &packet = (i % 2 == 0) ? add : del;
        hft::put_be(packet, 5, 4 + i, 6);
        return engine->process(packet, sink);
    };
    for (std::uint64_t i = 0; i < 10000; ++i)
        if (!cycle(i))
            return 3;
    auto measured = std::make_unique<hft::Engine<hft::Maker>>(c, clock.get(), metrics.get());
    test::start(*measured);
    measured->process(test::add(1, hft::Side::Buy, 100000, 1000, 2));
    measured->process(test::add(2, hft::Side::Sell, 100004, 1000, 3));
    // Reuse the warmed engine state with metrics enabled after warming its own branch/caches.
    for (std::uint64_t i = 0; i < 10000; ++i) {
        auto &packet = i % 2 == 0 ? add : del;
        hft::put_be(packet, 5, 4 + i, 6);
        if (!measured->process(packet, sink))
            return 3;
    }
    metrics->reset();
    auto before_orders = measured->oms.submitted;
    allocations = 0;
    audit = true;
    auto start = hft::monotonic_ns();
    for (std::uint64_t i = 0; i < samples; ++i) {
        auto &packet = i % 2 == 0 ? add : del;
        hft::put_be(packet, 5, 10004 + i, 6);
        if (!measured->process(packet, sink))
            return 3;
    }
    auto elapsed = hft::monotonic_ns() - start;
    audit = false;
    auto hot_allocations = allocations.load();
    // Separate uninstrumented throughput run; timer/histogram overhead must not be hidden.
    auto start_raw = hft::monotonic_ns();
    for (std::uint64_t i = 0; i < samples; ++i)
        if (!cycle(10000 + i))
            return 3;
    auto raw_elapsed = hft::monotonic_ns() - start_raw;
    auto micro = std::make_unique<hft::Histogram>();
    hft::Book book(1024, 1024, 1, dense);
    for (std::uint64_t i = 10; i < 110; ++i)
        book.apply(test::event(i, hft::Side::Buy, 99950 + static_cast<hft::Price>(i), 100));
    auto a = test::event(1, hft::Side::Buy, 100000, 1);
    auto x = test::removal(hft::Kind::Delete, 1);
    for (std::uint64_t i = 0; i < samples; ++i) {
        auto t = clock->stamp();
        auto ok = book.apply(a);
        auto end = clock->stamp();
        if (!ok)
            return 3;
        micro->record(clock->elapsed(t, end));
        if (!book.apply(x))
            return 3;
    }
    utsname host{};
    uname(&host);
    std::ofstream out(prefix + ".json");
    out << "{\"schema\":2,\"dataset\":\"two_sided_forced_quote_v1\",\"platform\":\"" << host.sysname
        << "\",\"arch\":\"" << host.machine << "\",\"compiler\":\"" << __VERSION__
        << "\",\"build_type\":\"" << HFT_BUILD_TYPE
        << "\",\"native\":" << (HFT_NATIVE_ENABLED ? "true" : "false")
        << ",\"pinned\":" << (pinned ? "true" : "false") << ",\"timer\":\""
        << (clock->tsc() ? "invariant_tsc" : "steady_clock")
        << "\",\"ns_per_tick\":" << clock->ns_per_tick()
        << ",\"dense_index\":" << (dense ? "true" : "false")
        << ",\"hot_allocations\":" << hot_allocations
        << ",\"encoded_orders\":" << measured->oms.submitted - before_orders
        << ",\"instrumented_messages_per_second\":" << double(samples) * 1e9 / double(elapsed)
        << ",\"messages_per_second\":" << double(samples) * 1e9 / double(raw_elapsed)
        << ",\"stages\":";
    metrics->json(out);
    out << ",\"book_add_micro\":";
    micro->json(out);
    out << "}\n";
    std::ofstream csv(prefix + ".csv");
    metrics->csv(csv);
    std::cout << "Orders=" << measured->oms.submitted - before_orders
              << " allocations=" << hot_allocations << " report=" << prefix << ".json\n";
    return hot_allocations || measured->oms.submitted == before_orders ? 4 : 0;
}
