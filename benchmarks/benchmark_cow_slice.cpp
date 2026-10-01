#include <benchmark/benchmark.h>
#include "kvasir/utf8_cow.hpp"
#include "kvasir/utf8_slice.hpp"

using namespace kvasir;

static void BM_SliceCreationString(benchmark::State& state) {
    utf8_string str("This is a somewhat long string for benchmarking.");
    for (auto _ : state) {
        utf8_slice slice(str);
        benchmark::DoNotOptimize(slice);
    }
}
BENCHMARK(BM_SliceCreationString);

static void BM_SliceCreationRope(benchmark::State& state) {
    utf8_rope rope(utf8_string("This is a somewhat long string for benchmarking."));
    for (auto _ : state) {
        utf8_slice slice(rope);
        benchmark::DoNotOptimize(slice);
    }
}
BENCHMARK(BM_SliceCreationRope);

static void BM_SliceIteration(benchmark::State& state) {
    utf8_string str("This is a somewhat long string for benchmarking.");
    utf8_slice slice(str);
    for (auto _ : state) {
        for (size_t i = 0; i < slice.length(); ++i) {
            benchmark::DoNotOptimize(slice[i]);
        }
    }
}
BENCHMARK(BM_SliceIteration);

static void BM_CowBorrowString(benchmark::State& state) {
    utf8_string str("This is a somewhat long string for benchmarking.");
    for (auto _ : state) {
        utf8_cow cow(str);
        benchmark::DoNotOptimize(cow);
    }
}
BENCHMARK(BM_CowBorrowString);

static void BM_CowBorrowAndMutate(benchmark::State& state) {
    utf8_string str("This is a somewhat long string for benchmarking.");
    for (auto _ : state) {
        utf8_cow cow(str);
        cow.push_back('A');
        benchmark::DoNotOptimize(cow);
    }
}
BENCHMARK(BM_CowBorrowAndMutate);
