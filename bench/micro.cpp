#include "../tests/support.hpp"
#include "hft/book/book.hpp"
#include <benchmark/benchmark.h>
static void Parser(benchmark::State &state) {
    auto bytes = test::add(1, hft::Side::Buy, 100000, 100);
    hft::Event event;
    for (auto _ : state) {
        (void)_;
        benchmark::DoNotOptimize(hft::parse(bytes, event));
        benchmark::DoNotOptimize(event);
    }
}
static void BookAddCancel(benchmark::State &state) {
    hft::Book book(1024, 1024);
    auto a = test::event(1, hft::Side::Buy, 100000, 1), x = test::removal(hft::Kind::Delete, 1);
    for (auto _ : state) {
        (void)_;
        benchmark::DoNotOptimize(book.apply(a));
        benchmark::DoNotOptimize(book.apply(x));
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(Parser);
BENCHMARK(BookAddCancel);
BENCHMARK_MAIN();
