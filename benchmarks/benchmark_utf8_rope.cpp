#include <benchmark/benchmark.h>
#include <kvasir/utf8_rope.hpp>
#include <kvasir/utf8_string.hpp>
#include <string>

static void BM_RopeInsert(benchmark::State& state) {
    for (auto _ : state) {
        kvasir::utf8_rope rope;
        for (int i = 0; i < state.range(0); ++i) {
            rope = rope.insert(rope.length(), kvasir::utf8_rope("a"));
        }
        benchmark::DoNotOptimize(rope);
    }
}
BENCHMARK(BM_RopeInsert)->Range(8, 1024);

static void BM_Utf8StringInsert(benchmark::State& state) {
    for (auto _ : state) {
        kvasir::utf8_string str;
        for (int i = 0; i < state.range(0); ++i) {
            str.insert(str.length(), kvasir::utf8_string("a"));
        }
        benchmark::DoNotOptimize(str);
    }
}
BENCHMARK(BM_Utf8StringInsert)->Range(8, 1024);

static void BM_StdStringInsert(benchmark::State& state) {
    for (auto _ : state) {
        std::string str;
        for (int i = 0; i < state.range(0); ++i) {
            str.insert(str.length(), "a");
        }
        benchmark::DoNotOptimize(str);
    }
}
BENCHMARK(BM_StdStringInsert)->Range(8, 1024);

static void BM_RopeConcat(benchmark::State& state) {
    kvasir::utf8_rope r1("hello 🌍!");
    kvasir::utf8_rope r2("hello 🌍!");
    for (auto _ : state) {
        kvasir::utf8_rope rope;
        for (int i = 0; i < state.range(0); ++i) {
            rope = rope + r1 + r2;
        }
        benchmark::DoNotOptimize(rope);
    }
}
BENCHMARK(BM_RopeConcat)->Range(8, 1024);

static void BM_Utf8StringConcat(benchmark::State& state) {
    kvasir::utf8_string r1("hello 🌍!");
    kvasir::utf8_string r2("hello 🌍!");
    for (auto _ : state) {
        kvasir::utf8_string str;
        for (int i = 0; i < state.range(0); ++i) {
            str.insert(str.length(), r1);
            str.insert(str.length(), r2);
        }
        benchmark::DoNotOptimize(str);
    }
}
BENCHMARK(BM_Utf8StringConcat)->Range(8, 1024);

static void BM_StdStringConcat(benchmark::State& state) {
    std::string r1("hello 🌍!");
    std::string r2("hello 🌍!");
    for (auto _ : state) {
        std::string str;
        for (int i = 0; i < state.range(0); ++i) {
            str += r1;
            str += r2;
        }
        benchmark::DoNotOptimize(str);
    }
}
BENCHMARK(BM_StdStringConcat)->Range(8, 1024);
