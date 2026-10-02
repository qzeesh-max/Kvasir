// benchmark_perf_deep.cpp ─ Profiling-driven micro-benchmarks for Kvasir
//
// Goals:
//   1. Measure push_back throughput (ASCII / 2-byte / 4-byte code points).
//   2. substr / erase / replace at various positions.
//   3. Mixed-width random-access indexing (the hot path for the fragment index).
//   4. Rope: deep trees built by repeated insert-at-middle vs. linear concat.
//   5. Pool allocator: warm vs. cold allocation.
//   6. validate_utf8 on large mixed-encoding inputs.
//   7. Batch decode_code_points vs. individual operator[].

#include <benchmark/benchmark.h>
#include <kvasir/utf8_string.hpp>
#include <kvasir/utf8_rope.hpp>

#include <string>
#include <vector>
#include <random>
#include <cstring>

// ─── Test data generators ────────────────────────────────────────────────────

// Generate a UTF-8 string with a mix of 1-, 2-, 3-, 4-byte code points
// (ASCII, Latin-1, CJK, emoji) repeated `n_chars` times.
static std::string make_mixed_utf8(int n_chars) {
    // Pattern: ASCII 'A', Latin-1 e-acute (U+00E9, 2-byte), CJK zhong (U+4E2D, 3-byte),
    //          earth globe (U+1F30D, 4-byte)
    const int pattern_cps = 4;
    std::string result;
    result.reserve(n_chars * 3);
    for (int i = 0; i < n_chars; ++i) {
        int idx = i % pattern_cps;
        switch (idx) {
            case 0: result += 'A'; break;
            case 1: result += "\xC3\xA9"; break;
            case 2: result += "\xE4\xB8\xAD"; break;
            case 3: result += "\xF0\x9F\x8C\x8D"; break;
        }
    }
    return result;
}

static std::string make_ascii_string(int n) {
    return std::string(n, 'x');
}

// ─── 1. push_back throughput ─────────────────────────────────────────────────

static void BM_PushBack_ASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        kvasir::utf8_string s;
        for (int i = 0; i < n; ++i) s.push_back('A');
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_PushBack_ASCII)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_PushBack_StdString(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::string s;
        for (int i = 0; i < n; ++i) s.push_back('A');
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_PushBack_StdString)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_PushBack_TwoByte(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        kvasir::utf8_string s;
        for (int i = 0; i < n; ++i) s.push_back(0x00E9); // e-acute
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_PushBack_TwoByte)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_PushBack_FourByte(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        kvasir::utf8_string s;
        for (int i = 0; i < n; ++i) s.push_back(0x1F600); // grinning face
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_PushBack_FourByte)->Range(64, 4096)->Unit(benchmark::kNanosecond);

// ─── 2. append chunk throughput ──────────────────────────────────────────────

static void BM_Append_ChunkASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string chunk(64, 'x');
    for (auto _ : state) {
        kvasir::utf8_string s;
        for (int i = 0; i < n; ++i) s.append(chunk);
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations() * n * 64);
}
BENCHMARK(BM_Append_ChunkASCII)->Range(8, 256)->Unit(benchmark::kNanosecond);

static void BM_Append_ChunkMixed(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string chunk = make_mixed_utf8(16);
    for (auto _ : state) {
        kvasir::utf8_string s;
        for (int i = 0; i < n; ++i) s.append(chunk);
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations() * n * 16);
}
BENCHMARK(BM_Append_ChunkMixed)->Range(8, 256)->Unit(benchmark::kNanosecond);

// ─── 3. substr ───────────────────────────────────────────────────────────────

static void BM_Substr_ASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string s(make_ascii_string(n));
    size_t pos = n / 4, len = n / 2;
    for (auto _ : state) {
        auto sub = s.substr(pos, len);
        benchmark::DoNotOptimize(sub);
    }
    state.SetBytesProcessed(state.iterations() * len);
}
BENCHMARK(BM_Substr_ASCII)->Range(64, 8192)->Unit(benchmark::kNanosecond);

static void BM_Substr_Mixed(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string s(make_mixed_utf8(n));
    size_t pos = n / 4, len = n / 2;
    for (auto _ : state) {
        auto sub = s.substr(pos, len);
        benchmark::DoNotOptimize(sub);
    }
    state.SetItemsProcessed(state.iterations() * len);
}
BENCHMARK(BM_Substr_Mixed)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_Substr_StdString(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string s(make_ascii_string(n));
    size_t pos = n / 4, len = n / 2;
    for (auto _ : state) {
        auto sub = s.substr(pos, len);
        benchmark::DoNotOptimize(sub);
    }
    state.SetBytesProcessed(state.iterations() * len);
}
BENCHMARK(BM_Substr_StdString)->Range(64, 8192)->Unit(benchmark::kNanosecond);

// ─── 4. erase ────────────────────────────────────────────────────────────────

static void BM_Erase_Middle_ASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string base = make_ascii_string(n);
    for (auto _ : state) {
        kvasir::utf8_string s(base);
        s.erase(n / 4, n / 2);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_Erase_Middle_ASCII)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_Erase_Middle_Mixed(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string base = make_mixed_utf8(n);
    for (auto _ : state) {
        kvasir::utf8_string s(base);
        s.erase(n / 4, n / 2);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_Erase_Middle_Mixed)->Range(64, 2048)->Unit(benchmark::kNanosecond);

static void BM_Erase_Middle_StdString(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string base = make_ascii_string(n);
    for (auto _ : state) {
        std::string s(base);
        s.erase(n / 4, n / 2);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_Erase_Middle_StdString)->Range(64, 4096)->Unit(benchmark::kNanosecond);

// ─── 5. replace ──────────────────────────────────────────────────────────────

static void BM_Replace_ASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string base = make_ascii_string(n);
    kvasir::utf8_string replacement("REPLACED");
    for (auto _ : state) {
        kvasir::utf8_string s(base);
        s.replace(n / 4, 8, replacement);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_Replace_ASCII)->Range(64, 4096)->Unit(benchmark::kNanosecond);

// ─── 6. Mixed-width random access (fragment index hot path) ──────────────────

static void BM_RandomAccess_Mixed(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string s(make_mixed_utf8(n));
    size_t len = s.length();
    std::mt19937 rng(42);
    std::vector<size_t> indices(256);
    for (auto& idx : indices) idx = rng() % len;

    for (auto _ : state) {
        uint32_t sum = 0;
        for (size_t idx : indices) sum += s[idx];
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 256);
}
BENCHMARK(BM_RandomAccess_Mixed)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_RandomAccess_ASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string s(make_ascii_string(n));
    size_t len = s.length();
    std::mt19937 rng(42);
    std::vector<size_t> indices(256);
    for (auto& idx : indices) idx = rng() % len;

    for (auto _ : state) {
        uint32_t sum = 0;
        for (size_t idx : indices) sum += s[idx];
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 256);
}
BENCHMARK(BM_RandomAccess_ASCII)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_RandomAccess_StdString(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string s(make_ascii_string(n));
    std::mt19937 rng(42);
    std::vector<size_t> indices(256);
    for (auto& idx : indices) idx = rng() % (size_t)n;

    for (auto _ : state) {
        unsigned char sum = 0;
        for (size_t idx : indices) sum += (unsigned char)s[idx];
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 256);
}
BENCHMARK(BM_RandomAccess_StdString)->Range(64, 4096)->Unit(benchmark::kNanosecond);

// ─── 7. Sequential scan (iteration) ──────────────────────────────────────────

static void BM_Iteration_Mixed_Large(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string s(make_mixed_utf8(n));
    for (auto _ : state) {
        uint32_t sum = 0;
        for (uint32_t cp : s) sum += cp;
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_Iteration_Mixed_Large)->Range(64, 8192)->Unit(benchmark::kNanosecond);

// ─── 8. Batch decode vs. individual operator[] ───────────────────────────────

static void BM_BatchDecode_Batch(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string s(make_mixed_utf8(n));
    size_t len = s.length();
    std::vector<uint32_t> out(len);
    for (auto _ : state) {
        size_t decoded = s.decode_code_points(0, len, out.data());
        benchmark::DoNotOptimize(decoded);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * len);
}
BENCHMARK(BM_BatchDecode_Batch)->Range(64, 4096)->Unit(benchmark::kNanosecond);

static void BM_BatchDecode_Individual(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string s(make_mixed_utf8(n));
    size_t len = s.length();
    for (auto _ : state) {
        uint32_t sum = 0;
        for (size_t i = 0; i < len; ++i) sum += s[i];
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * len);
}
BENCHMARK(BM_BatchDecode_Individual)->Range(64, 4096)->Unit(benchmark::kNanosecond);

// ─── 9. Rope operations ──────────────────────────────────────────────────────

static void BM_Rope_LinearAppend(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_rope chunk("hello");
    for (auto _ : state) {
        kvasir::utf8_rope r;
        for (int i = 0; i < n; ++i) r += chunk;
        benchmark::DoNotOptimize(r.length());
    }
}
BENCHMARK(BM_Rope_LinearAppend)->Range(8, 512)->Unit(benchmark::kNanosecond);

static void BM_Rope_InsertAtMiddle(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_rope chunk("hi");
    for (auto _ : state) {
        kvasir::utf8_rope r;
        for (int i = 0; i < n; ++i) {
            r = r.insert(r.length() / 2, chunk);
        }
        benchmark::DoNotOptimize(r.length());
    }
}
BENCHMARK(BM_Rope_InsertAtMiddle)->Range(8, 256)->Unit(benchmark::kNanosecond);

static void BM_Rope_ToStringDeepTree(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_rope chunk("x");
    kvasir::utf8_rope r;
    for (int i = 0; i < n; ++i) r = r.insert(r.length() / 2, chunk);
    for (auto _ : state) {
        auto str = r.to_string();
        benchmark::DoNotOptimize(str);
    }
    state.SetBytesProcessed(state.iterations() * r.byte_size());
}
BENCHMARK(BM_Rope_ToStringDeepTree)->Range(8, 256)->Unit(benchmark::kNanosecond);

static void BM_Rope_RandomAccess(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_rope chunk("hello");
    kvasir::utf8_rope r;
    for (int i = 0; i < n; ++i) r += chunk;
    size_t len = r.length();
    std::mt19937 rng(42);
    std::vector<size_t> indices(256);
    for (auto& idx : indices) idx = rng() % len;
    for (auto _ : state) {
        uint32_t sum = 0;
        for (size_t idx : indices) sum += r[idx];
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 256);
}
BENCHMARK(BM_Rope_RandomAccess)->Range(8, 512)->Unit(benchmark::kNanosecond);

static void BM_Rope_Erase(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_rope chunk("hello");
    kvasir::utf8_rope base;
    for (int i = 0; i < n; ++i) base += chunk;
    size_t len = base.length();
    for (auto _ : state) {
        auto r = base.erase(len / 4, len / 2);
        benchmark::DoNotOptimize(r.length());
    }
}
BENCHMARK(BM_Rope_Erase)->Range(8, 512)->Unit(benchmark::kNanosecond);

// ─── 10. validate_utf8 on large inputs ────────────────────────────────────────

static void BM_ValidateUtf8_MixedLarge(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string data = make_mixed_utf8(n);
    for (auto _ : state) {
        bool v = kvasir::utf8_string::validate_utf8(data);
        benchmark::DoNotOptimize(v);
    }
    state.SetBytesProcessed(state.iterations() * (long long)data.size());
}
BENCHMARK(BM_ValidateUtf8_MixedLarge)->Range(64, 65536)->Unit(benchmark::kNanosecond);

static void BM_ValidateUtf8_ASCIILarge(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string data(n, 'A');
    for (auto _ : state) {
        bool v = kvasir::utf8_string::validate_utf8(data);
        benchmark::DoNotOptimize(v);
    }
    state.SetBytesProcessed(state.iterations() * (long long)n);
}
BENCHMARK(BM_ValidateUtf8_ASCIILarge)->Range(64, 65536)->Unit(benchmark::kNanosecond);

// ─── 11. Pool allocator: warm vs. cold ────────────────────────────────────────

static void BM_PoolAlloc_Warm(benchmark::State& state) {
    {
        std::vector<kvasir::utf8_rope*> tmp;
        for (int i = 0; i < 300; ++i) tmp.push_back(new kvasir::utf8_rope("warm"));
        for (auto* p : tmp) delete p;
    }
    int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<kvasir::utf8_rope*> ropes;
        ropes.reserve(n);
        for (int i = 0; i < n; ++i) ropes.push_back(new kvasir::utf8_rope("x"));
        for (auto* p : ropes) delete p;
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_PoolAlloc_Warm)->Range(16, 512)->Unit(benchmark::kNanosecond);

static void BM_PoolAlloc_RopeInsertReuse(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        kvasir::utf8_rope r;
        for (int i = 0; i < n; ++i) {
            r = r.insert(r.length() / 2, kvasir::utf8_rope("x"));
        }
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_PoolAlloc_RopeInsertReuse)->Range(16, 256)->Unit(benchmark::kNanosecond);

// ─── 12. String creation at scale ─────────────────────────────────────────────

static void BM_Creation_LargeASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string data = make_ascii_string(n);
    for (auto _ : state) {
        kvasir::utf8_string s(data);
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * (long long)n);
}
BENCHMARK(BM_Creation_LargeASCII)->Range(64, 65536)->Unit(benchmark::kNanosecond);

static void BM_Creation_LargeMixed(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string data = make_mixed_utf8(n);
    for (auto _ : state) {
        kvasir::utf8_string s(data);
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations() * (long long)n);
}
BENCHMARK(BM_Creation_LargeMixed)->Range(64, 16384)->Unit(benchmark::kNanosecond);

static void BM_Creation_StdStringLarge(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string data = make_ascii_string(n);
    for (auto _ : state) {
        std::string s(data);
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * (long long)n);
}
BENCHMARK(BM_Creation_StdStringLarge)->Range(64, 65536)->Unit(benchmark::kNanosecond);

// ─── 13. Find at various positions ────────────────────────────────────────────

static void BM_Find_NearEnd_ASCII(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    std::string base = make_ascii_string(n);
    // Place a unique suffix near end
    base[n - 2] = 'Z';
    base[n - 1] = 'Q';
    kvasir::utf8_string haystack(base);
    kvasir::utf8_string needle("ZQ");
    for (auto _ : state) {
        benchmark::DoNotOptimize(haystack.find(needle));
    }
    state.SetBytesProcessed(state.iterations() * (long long)base.size());
}
BENCHMARK(BM_Find_NearEnd_ASCII)->Range(64, 65536)->Unit(benchmark::kNanosecond);

static void BM_Find_NearEnd_Mixed(benchmark::State& state) {
    int n = static_cast<int>(state.range(0));
    kvasir::utf8_string haystack(make_mixed_utf8(n));
    // Search for emoji (4-byte, appears every 4 cps)
    kvasir::utf8_string needle("\xF0\x9F\x8C\x8D");
    for (auto _ : state) {
        benchmark::DoNotOptimize(haystack.find(needle));
    }
    state.SetItemsProcessed(state.iterations() * (long long)n);
}
BENCHMARK(BM_Find_NearEnd_Mixed)->Range(64, 4096)->Unit(benchmark::kNanosecond);

