#include <benchmark/benchmark.h>
#include <kvasir/utf8_string.hpp>
#include <kvasir/utf8_string_view.hpp>
#include <string>
#include <vector>

const std::string test_data_utf8 = "Lorem ipsum dolor sit amet, 🌍 consectetur adipiscing elit. Még egy kis szöveg, hogy hosszabb legyen. And some more 🚀 and more and more...";
const std::wstring test_data_wide = L"Lorem ipsum dolor sit amet, 🌍 consectetur adipiscing elit. Még egy kis szöveg, hogy hosszabb legyen. And some more 🚀 and more and more...";

static void BM_StdStringCreation(benchmark::State& state) {
  for (auto _ : state) {
    std::string s(test_data_utf8);
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_StdStringCreation);

static void BM_StdWstringCreation(benchmark::State& state) {
  for (auto _ : state) {
    std::wstring s(test_data_wide);
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_StdWstringCreation);

static void BM_KvasirUtf8StringCreation(benchmark::State& state) {
  for (auto _ : state) {
    kvasir::utf8_string s(test_data_utf8);
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_KvasirUtf8StringCreation);

static void BM_StdStringIteration(benchmark::State& state) {
  std::string s(test_data_utf8);
  for (auto _ : state) {
    for (char c : s) {
      benchmark::DoNotOptimize(c);
    }
  }
}
BENCHMARK(BM_StdStringIteration);

static void BM_StdWstringIteration(benchmark::State& state) {
  std::wstring s(test_data_wide);
  for (auto _ : state) {
    for (wchar_t c : s) {
      benchmark::DoNotOptimize(c);
    }
  }
}
BENCHMARK(BM_StdWstringIteration);

static void BM_KvasirUtf8StringIteration(benchmark::State& state) {
  kvasir::utf8_string s(test_data_utf8);
  for (auto _ : state) {
    for (uint32_t c : s) {
      benchmark::DoNotOptimize(c);
    }
  }
}
BENCHMARK(BM_KvasirUtf8StringIteration);

static void BM_StdStringIndexing(benchmark::State& state) {
  std::string s(test_data_utf8);
  size_t len = s.length();
  for (auto _ : state) {
    for (size_t i = 0; i < len; ++i) {
      benchmark::DoNotOptimize(s[i]);
    }
  }
}
BENCHMARK(BM_StdStringIndexing);

static void BM_StdWstringIndexing(benchmark::State& state) {
  std::wstring s(test_data_wide);
  size_t len = s.length();
  for (auto _ : state) {
    for (size_t i = 0; i < len; ++i) {
      benchmark::DoNotOptimize(s[i]);
    }
  }
}
BENCHMARK(BM_StdWstringIndexing);

static void BM_KvasirUtf8StringIndexing(benchmark::State& state) {
  kvasir::utf8_string s(test_data_utf8);
  size_t len = s.length();
  for (auto _ : state) {
    for (size_t i = 0; i < len; ++i) {
      benchmark::DoNotOptimize(s[i]);
    }
  }
}
BENCHMARK(BM_KvasirUtf8StringIndexing);

static void BM_KvasirUtf8StringBatchDecoding(benchmark::State& state) {
  kvasir::utf8_string s(test_data_utf8);
  size_t len = s.length();
  uint32_t buf[256]; // Large enough for our test string
  for (auto _ : state) {
    size_t decoded = s.decode_code_points(0, len, buf);
    benchmark::DoNotOptimize(decoded);
    benchmark::DoNotOptimize(buf);
  }
}
BENCHMARK(BM_KvasirUtf8StringBatchDecoding);

static void BM_KvasirUtf8StringValidation(benchmark::State& state) {
  for (auto _ : state) {
    bool valid = kvasir::utf8_string::validate_utf8(test_data_utf8);
    benchmark::DoNotOptimize(valid);
  }
}
BENCHMARK(BM_KvasirUtf8StringValidation);

BENCHMARK_MAIN();
