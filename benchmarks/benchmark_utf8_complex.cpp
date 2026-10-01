#include <benchmark/benchmark.h>
#include <kvasir/utf8_string.hpp>
#include <kvasir/utf8_rope.hpp>
#include <string>
#include <unordered_map>
#include <map>

// Emojis, English, Arabic, Chinese
const char* multi_lingual_haystack = "Hello world! This is a longer piece of text. We will embed some emojis here: 🚀 🌟 💻. "
                                     "Now some Arabic: مرحبا بالعالم. "
                                     "And some Chinese: 你好，世界！ "
                                     "Let's see how fast we can find a needle in this haystack.";

const char* needle_emoji = "💻";
const char* needle_arabic = "بالعالم";
const char* needle_chinese = "世界";

static void BM_NeedleInHaystack_StdString(benchmark::State& state) {
    std::string haystack(multi_lingual_haystack);
    std::string needle(needle_chinese);
    for (auto _ : state) {
        benchmark::DoNotOptimize(haystack.find(needle));
    }
}
BENCHMARK(BM_NeedleInHaystack_StdString);

static void BM_NeedleInHaystack_Utf8String(benchmark::State& state) {
    kvasir::utf8_string haystack(multi_lingual_haystack);
    kvasir::utf8_string needle(needle_chinese);
    for (auto _ : state) {
        benchmark::DoNotOptimize(haystack.find(needle));
    }
}
BENCHMARK(BM_NeedleInHaystack_Utf8String);

static void BM_NeedleInHaystack_Utf8Rope(benchmark::State& state) {
    kvasir::utf8_rope haystack(multi_lingual_haystack);
    kvasir::utf8_string needle(needle_chinese);
    for (auto _ : state) {
        benchmark::DoNotOptimize(haystack.find(needle));
    }
}
BENCHMARK(BM_NeedleInHaystack_Utf8Rope);

// Uppercase ASCII transformation logic benchmarks
static void BM_AsciiToUpper_StdString(benchmark::State& state) {
    std::string str(multi_lingual_haystack);
    for (auto _ : state) {
        std::string upper;
        upper.reserve(str.size());
        for (char c : str) {
            upper.push_back((c >= 'a' && c <= 'z') ? c - 32 : c);
        }
        benchmark::DoNotOptimize(upper);
    }
}
BENCHMARK(BM_AsciiToUpper_StdString);

static void BM_AsciiToUpper_Utf8String(benchmark::State& state) {
    kvasir::utf8_string str(multi_lingual_haystack);
    for (auto _ : state) {
        std::string upper_bytes;
        // Approximation of the string building process with iteration
        for (auto cp : str) {
            if (cp >= 'a' && cp <= 'z') {
                upper_bytes.push_back(static_cast<char>(cp - 32));
            } else if (cp <= 0x7F) {
                upper_bytes.push_back(static_cast<char>(cp));
            } else {
                char buf[5] = {0};
                if (cp <= 0x7FF) {
                    buf[0] = 0xC0 | ((cp >> 6) & 0x1F);
                    buf[1] = 0x80 | (cp & 0x3F);
                } else if (cp <= 0xFFFF) {
                    buf[0] = 0xE0 | ((cp >> 12) & 0x0F);
                    buf[1] = 0x80 | ((cp >> 6) & 0x3F);
                    buf[2] = 0x80 | (cp & 0x3F);
                } else {
                    buf[0] = 0xF0 | ((cp >> 18) & 0x07);
                    buf[1] = 0x80 | ((cp >> 12) & 0x3F);
                    buf[2] = 0x80 | ((cp >> 6) & 0x3F);
                    buf[3] = 0x80 | (cp & 0x3F);
                }
                upper_bytes.append(buf);
            }
        }
        kvasir::utf8_string upper(upper_bytes);
        benchmark::DoNotOptimize(upper);
    }
}
BENCHMARK(BM_AsciiToUpper_Utf8String);

static void BM_UnorderedMap_StdString(benchmark::State& state) {
    std::unordered_map<std::string, int> dict;
    dict["Hello"] = 1;
    dict["World"] = 2;
    dict["💻"] = 3;
    dict["你好"] = 4;
    
    std::string keys[] = {"Hello", "World", "💻", "你好"};
    for (auto _ : state) {
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(dict[key]);
        }
    }
}
BENCHMARK(BM_UnorderedMap_StdString);

static void BM_UnorderedMap_Utf8String(benchmark::State& state) {
    std::unordered_map<kvasir::utf8_string, int> dict;
    dict[kvasir::utf8_string("Hello")] = 1;
    dict[kvasir::utf8_string("World")] = 2;
    dict[kvasir::utf8_string("💻")] = 3;
    dict[kvasir::utf8_string("你好")] = 4;
    
    kvasir::utf8_string keys[] = {
        kvasir::utf8_string("Hello"),
        kvasir::utf8_string("World"),
        kvasir::utf8_string("💻"),
        kvasir::utf8_string("你好")
    };
    for (auto _ : state) {
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(dict[key]);
        }
    }
}
BENCHMARK(BM_UnorderedMap_Utf8String);

static void BM_UnorderedMap_Utf8Rope(benchmark::State& state) {
    std::unordered_map<kvasir::utf8_rope, int> dict;
    dict[kvasir::utf8_rope("Hello")] = 1;
    dict[kvasir::utf8_rope("World")] = 2;
    dict[kvasir::utf8_rope("💻")] = 3;
    dict[kvasir::utf8_rope("你好")] = 4;
    
    kvasir::utf8_rope keys[] = {
        kvasir::utf8_rope("Hello"),
        kvasir::utf8_rope("World"),
        kvasir::utf8_rope("💻"),
        kvasir::utf8_rope("你好")
    };
    for (auto _ : state) {
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(dict[key]);
        }
    }
}
BENCHMARK(BM_UnorderedMap_Utf8Rope);

static void BM_OrderedMap_StdString(benchmark::State& state) {
    std::map<std::string, int> dict;
    dict["Hello"] = 1;
    dict["World"] = 2;
    dict["💻"] = 3;
    dict["你好"] = 4;
    
    std::string keys[] = {"Hello", "World", "💻", "你好"};
    for (auto _ : state) {
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(dict[key]);
        }
    }
}
BENCHMARK(BM_OrderedMap_StdString);

static void BM_OrderedMap_Utf8String(benchmark::State& state) {
    std::map<kvasir::utf8_string, int> dict;
    dict[kvasir::utf8_string("Hello")] = 1;
    dict[kvasir::utf8_string("World")] = 2;
    dict[kvasir::utf8_string("💻")] = 3;
    dict[kvasir::utf8_string("你好")] = 4;
    
    kvasir::utf8_string keys[] = {
        kvasir::utf8_string("Hello"),
        kvasir::utf8_string("World"),
        kvasir::utf8_string("💻"),
        kvasir::utf8_string("你好")
    };
    for (auto _ : state) {
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(dict[key]);
        }
    }
}
BENCHMARK(BM_OrderedMap_Utf8String);
