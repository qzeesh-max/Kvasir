// test_perf_correctness.cpp ─ Correctness tests targeting hot paths found by
// profiling and benchmarking.
//
// Sections:
//  A. push_back edge cases and index integrity after high-volume push_back.
//  B. append_index correctness at fragment boundaries.
//  C. substr round-trip — byte-level and code-point-level fidelity.
//  D. erase at every valid position.
//  E. replace correctness with same-size, shorter, and longer replacements.
//  F. Batch decode (decode_code_points) vs. individual operator[] exhaustive.
//  G. Rope: deep-tree indexing correctness (insert-at-middle).
//  H. Rope: erase/split/concat round-trip invariant.
//  I. validate_utf8 exhaustive boundary checks.
//  J. Large-string index fragmentation: verify fragment transitions.
//  K. Iterator arithmetic on mixed-width strings.
//  L. String move-construct / move-assign preserves content.

#include <gtest/gtest.h>
#include <kvasir/utf8_string.hpp>
#include <kvasir/utf8_rope.hpp>

#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <numeric>

using namespace kvasir;

// ─── Utilities ───────────────────────────────────────────────────────────────

// Build a kvasir::utf8_string from a vector of code points via push_back.
static utf8_string from_cps(const std::vector<uint32_t>& cps) {
    utf8_string s;
    for (uint32_t cp : cps) s.push_back(cp);
    return s;
}

// Collect all code points from a utf8_string via iterator.
static std::vector<uint32_t> iter_cps(const utf8_string& s) {
    std::vector<uint32_t> out;
    for (uint32_t cp : s) out.push_back(cp);
    return out;
}

// Collect all code points from a utf8_string via operator[].
static std::vector<uint32_t> index_cps(const utf8_string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.length(); ++i) out.push_back(s[i]);
    return out;
}

// Generate a predictable mixed-width sequence of N code-points.
static std::vector<uint32_t> gen_mixed_cps(int n) {
    std::vector<uint32_t> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) {
        switch (i % 4) {
            case 0: v.push_back(0x41 + (i % 26)); break;         // ASCII A-Z
            case 1: v.push_back(0x00C0 + (i % 64)); break;       // 2-byte
            case 2: v.push_back(0x4E00 + (i % 256)); break;      // 3-byte CJK
            case 3: v.push_back(0x1F600 + (i % 64)); break;      // 4-byte emoji
        }
    }
    return v;
}

// ─── A. push_back edge cases ─────────────────────────────────────────────────

TEST(PerfCorrectness, PushBack_FragmentBoundary) {
    // kCodePointsPerFragment = kFragmentDiffsSize + 1 = 60.
    // Push exactly 60, 61, 120, 121 code-points to cross fragment boundaries.
    for (int n : {59, 60, 61, 119, 120, 121, 180, 181}) {
        std::vector<uint32_t> cps = gen_mixed_cps(n);
        utf8_string s = from_cps(cps);
        ASSERT_EQ(s.length(), (size_t)n) << "n=" << n;
        for (int i = 0; i < n; ++i) {
            EXPECT_EQ(s[i], cps[i]) << "n=" << n << " i=" << i;
        }
    }
}

TEST(PerfCorrectness, PushBack_HighVolume_IndexIntegrity) {
    // Push 2000 mixed-width code-points, then verify all index positions.
    constexpr int N = 2000;
    std::vector<uint32_t> cps = gen_mixed_cps(N);
    utf8_string s = from_cps(cps);
    ASSERT_EQ(s.length(), (size_t)N);

    // Spot-check at multiples of fragment size (60) and at random positions.
    std::mt19937 rng(42);
    std::vector<size_t> positions;
    for (size_t p = 0; p < (size_t)N; p += 60) positions.push_back(p);
    for (size_t i = 0; i < 100; ++i) positions.push_back(rng() % N);

    for (size_t pos : positions) {
        EXPECT_EQ(s[pos], cps[pos]) << "pos=" << pos;
    }
}

TEST(PerfCorrectness, PushBack_MaxCodePoint) {
    utf8_string s;
    s.push_back(0x10FFFF); // max valid Unicode code point
    ASSERT_EQ(s.length(), 1u);
    EXPECT_EQ(s[0], 0x10FFFFu);
}

TEST(PerfCorrectness, PushBack_InvalidCodePoint) {
    utf8_string s("hello");
    s.push_back(0x110000); // Invalid code point (too large)
    s.push_back(0x200000); // Invalid code point
    ASSERT_EQ(s.length(), 5u); // Should silently ignore
}

TEST(PerfCorrectness, PushBack_AllWidths_SameString) {
    // Push one of each width, then verify all 4 are correct.
    utf8_string s;
    s.push_back('A');        // 1-byte
    s.push_back(0x00E9);     // 2-byte: é
    s.push_back(0x4E2D);     // 3-byte: 中
    s.push_back(0x1F600);    // 4-byte: 😀
    ASSERT_EQ(s.length(), 4u);
    EXPECT_EQ(s[0], (uint32_t)'A');
    EXPECT_EQ(s[1], 0x00E9u);
    EXPECT_EQ(s[2], 0x4E2Du);
    EXPECT_EQ(s[3], 0x1F600u);
}

// ─── B. append_index correctness at fragment boundaries ──────────────────────

TEST(PerfCorrectness, Append_CrossesFragmentBoundary) {
    // Build a string up to 50 cps, then append enough to cross the boundary at 60.
    std::vector<uint32_t> first50 = gen_mixed_cps(50);
    std::vector<uint32_t> next30  = gen_mixed_cps(30);
    // Offset the second batch to avoid identical code-points
    for (auto& cp : next30) cp = (cp + 7) | 0x41;

    utf8_string s = from_cps(first50);

    // Append via push_back (exercises append_index through single-cp path)
    for (uint32_t cp : next30) s.push_back(cp);

    ASSERT_EQ(s.length(), 80u);
    for (size_t i = 0; i < 50; ++i) EXPECT_EQ(s[i], first50[i]) << "i=" << i;
    for (size_t i = 0; i < 30; ++i) EXPECT_EQ(s[50 + i], next30[i]) << "i=" << i;
}

TEST(PerfCorrectness, Append_StringCrossesMultipleFragments) {
    // Append a 200-cp mixed string to a 50-cp string.
    utf8_string base = from_cps(gen_mixed_cps(50));
    std::string mixed200_bytes;
    auto cps200 = gen_mixed_cps(200);
    // Encode to UTF-8 bytes
    for (uint32_t cp : cps200) {
        if (cp <= 0x7F) {
            mixed200_bytes.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            mixed200_bytes.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            mixed200_bytes.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            mixed200_bytes.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            mixed200_bytes.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            mixed200_bytes.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            mixed200_bytes.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            mixed200_bytes.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            mixed200_bytes.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            mixed200_bytes.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    base.append(mixed200_bytes);
    ASSERT_EQ(base.length(), 250u);
    for (size_t i = 0; i < 200; ++i) {
        EXPECT_EQ(base[50 + i], cps200[i]) << "appended cp at i=" << i;
    }
}

// ─── C. substr round-trip ─────────────────────────────────────────────────────

TEST(PerfCorrectness, Substr_ExhaustivePositions) {
    std::vector<uint32_t> cps = gen_mixed_cps(60);
    utf8_string s = from_cps(cps);

    // For a subset of (pos, count) pairs, verify substr gives the right code-points.
    for (size_t pos = 0; pos <= 60; pos += 7) {
        for (size_t count : {(size_t)0, (size_t)1, (size_t)5, (size_t)20}) {
            utf8_string sub = s.substr(pos, count);
            size_t expected_len = std::min(count, 60 - std::min(pos, (size_t)60));
            ASSERT_EQ(sub.length(), expected_len)
                << "pos=" << pos << " count=" << count;
            for (size_t i = 0; i < expected_len; ++i) {
                EXPECT_EQ(sub[i], cps[pos + i])
                    << "pos=" << pos << " i=" << i;
            }
        }
    }
}

TEST(PerfCorrectness, Substr_NposEqualsEnd) {
    std::vector<uint32_t> cps = gen_mixed_cps(40);
    utf8_string s = from_cps(cps);
    utf8_string sub = s.substr(10);  // npos = to end
    ASSERT_EQ(sub.length(), 30u);
    for (size_t i = 0; i < 30; ++i) EXPECT_EQ(sub[i], cps[10 + i]);
}

TEST(PerfCorrectness, Substr_PosGeLength_ReturnsEmpty) {
    utf8_string s("hello");
    EXPECT_TRUE(s.substr(10).empty());
    EXPECT_TRUE(s.substr(5).empty());
}

// ─── D. erase at every valid position ────────────────────────────────────────

TEST(PerfCorrectness, Erase_EveryPosition_MixedWidth) {
    std::vector<uint32_t> cps = gen_mixed_cps(24);
    utf8_string base = from_cps(cps);

    for (size_t pos = 0; pos < 24; ++pos) {
        for (size_t count = 1; count <= 24 - pos; count += 3) {
            utf8_string s = base;
            s.erase(pos, count);
            size_t expected_len = 24 - count;
            ASSERT_EQ(s.length(), expected_len)
                << "pos=" << pos << " count=" << count;
            // Verify remaining prefix
            for (size_t i = 0; i < pos; ++i) {
                EXPECT_EQ(s[i], cps[i]) << "prefix i=" << i;
            }
            // Verify remaining suffix
            for (size_t i = pos; i < expected_len; ++i) {
                EXPECT_EQ(s[i], cps[i + count])
                    << "suffix i=" << i << " maps to cp[" << (i + count) << "]";
            }
        }
    }
}

TEST(PerfCorrectness, Erase_EntireString) {
    utf8_string s("Hello 世界");
    size_t len = s.length();
    s.erase(0, len);
    EXPECT_TRUE(s.empty());
}

TEST(PerfCorrectness, Erase_NposErasesToEnd) {
    std::vector<uint32_t> cps = gen_mixed_cps(20);
    utf8_string s = from_cps(cps);
    s.erase(10); // npos default
    ASSERT_EQ(s.length(), 10u);
    for (size_t i = 0; i < 10; ++i) EXPECT_EQ(s[i], cps[i]);
}

TEST(PerfCorrectness, Erase_LargeString_HeapIndexShrink) {
    // Generate a string large enough to populate heap_index_ (>1024 cps)
    std::vector<uint32_t> cps = gen_mixed_cps(2000);
    utf8_string s = from_cps(cps);
    
    // Erase enough to shrink the heap_index_ size.
    // 2000 code points = 16 fragments (128 code points each).
    // Erasing from 500 to end shrinks the fragments to 500/128 + 1 = 4 fragments.
    s.erase(500, std::string::npos);
    ASSERT_EQ(s.length(), 500u);
    for (size_t i = 0; i < 500; ++i) EXPECT_EQ(s[i], cps[i]);
}

// ─── E. replace correctness ───────────────────────────────────────────────────

TEST(PerfCorrectness, Replace_SameLength) {
    std::vector<uint32_t> cps = gen_mixed_cps(20);
    utf8_string s = from_cps(cps);
    utf8_string repl = from_cps({0x1F600, 0x1F601, 0x1F602}); // 4-byte emoji x3
    s.replace(5, 3, repl);
    ASSERT_EQ(s.length(), 20u);
    for (size_t i = 0; i < 5; ++i) EXPECT_EQ(s[i], cps[i]);
    EXPECT_EQ(s[5], 0x1F600u);
    EXPECT_EQ(s[6], 0x1F601u);
    EXPECT_EQ(s[7], 0x1F602u);
    for (size_t i = 8; i < 20; ++i) EXPECT_EQ(s[i], cps[i]);
}

TEST(PerfCorrectness, Replace_ShorterReplacement) {
    std::vector<uint32_t> cps = gen_mixed_cps(20);
    utf8_string s = from_cps(cps);
    utf8_string repl = from_cps({0x41}); // single ASCII 'A'
    s.replace(5, 5, repl); // replace 5 cps with 1
    ASSERT_EQ(s.length(), 16u);
    EXPECT_EQ(s[5], 0x41u);
    for (size_t i = 6; i < 16; ++i) EXPECT_EQ(s[i], cps[i + 4]);
}

TEST(PerfCorrectness, Replace_LongerReplacement) {
    std::vector<uint32_t> cps = gen_mixed_cps(20);
    utf8_string s = from_cps(cps);
    utf8_string repl = from_cps(gen_mixed_cps(10)); // replace 3 with 10
    s.replace(5, 3, repl);
    ASSERT_EQ(s.length(), 27u);
    for (size_t i = 0; i < 5; ++i) EXPECT_EQ(s[i], cps[i]);
    for (size_t i = 15; i < 27; ++i) EXPECT_EQ(s[i], cps[i - 7]);
}

TEST(PerfCorrectness, Replace_LargeString_HeapIndexShrink) {
    std::vector<uint32_t> cps = gen_mixed_cps(2000);
    utf8_string s = from_cps(cps);
    utf8_string repl = from_cps({0x41});
    // Replace from 500 to the end with a single character.
    // 2000 > 1024 so heap_index_ has fragments.
    // frag_idx for 500 is 500 / 128 = 3.
    // The replace will call partial_rebuild_index_from(500, byte_pos)
    // which shrinks the heap_index_ to size 3.
    s.replace(500, std::string::npos, repl);
    ASSERT_EQ(s.length(), 501u);
    EXPECT_EQ(s[500], 0x41u);
}

TEST(PerfCorrectness, Replace_NposAndBeyond) {
    std::vector<uint32_t> cps = gen_mixed_cps(20);
    utf8_string s1 = from_cps(cps);
    utf8_string repl = from_cps({0x41, 0x42}); // "AB"
    
    // Replace using npos
    s1.replace(10, std::string::npos, repl);
    ASSERT_EQ(s1.length(), 12u);
    EXPECT_EQ(s1[10], 0x41u);
    EXPECT_EQ(s1[11], 0x42u);
    
    // Replace with count extending past string length
    utf8_string s2 = from_cps(cps);
    s2.replace(15, 100, repl);
    ASSERT_EQ(s2.length(), 17u);
    EXPECT_EQ(s2[15], 0x41u);
    EXPECT_EQ(s2[16], 0x42u);
}

// ─── F. Batch decode vs. individual operator[] ───────────────────────────────

TEST(PerfCorrectness, BatchDecode_MatchesIndividual_Mixed) {
    constexpr int N = 500;
    std::vector<uint32_t> cps = gen_mixed_cps(N);
    utf8_string s = from_cps(cps);

    std::vector<uint32_t> batch_out(N);
    size_t decoded = s.decode_code_points(0, N, batch_out.data());
    ASSERT_EQ(decoded, (size_t)N);

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(batch_out[i], s[i]) << "i=" << i;
        EXPECT_EQ(batch_out[i], cps[i]) << "i=" << i;
    }
}

TEST(PerfCorrectness, BatchDecode_Partial) {
    std::vector<uint32_t> cps = gen_mixed_cps(100);
    utf8_string s = from_cps(cps);
    std::vector<uint32_t> out(30);
    size_t n = s.decode_code_points(20, 30, out.data());
    ASSERT_EQ(n, 30u);
    for (size_t i = 0; i < 30; ++i) EXPECT_EQ(out[i], cps[20 + i]);
}

TEST(PerfCorrectness, BatchDecode_PastEnd) {
    std::vector<uint32_t> cps = gen_mixed_cps(10);
    utf8_string s = from_cps(cps);
    std::vector<uint32_t> out(20);
    size_t n = s.decode_code_points(0, 20, out.data()); // request more than available
    EXPECT_EQ(n, 10u);
}

// ─── G. Rope deep-tree indexing correctness ───────────────────────────────────

TEST(PerfCorrectness, Rope_DeepTree_Indexing) {
    // Build a rope of N "x" chars via insert-at-middle, creating a balanced tree.
    constexpr int N = 100;
    utf8_rope r;
    utf8_rope chunk("x");
    for (int i = 0; i < N; ++i) r = r.insert(r.length() / 2, chunk);

    ASSERT_EQ(r.length(), (size_t)N);
    for (size_t i = 0; i < (size_t)N; ++i) {
        EXPECT_EQ(r[i], (uint32_t)'x') << "i=" << i;
    }
}

TEST(PerfCorrectness, Rope_DeepTree_ToStringCorrect) {
    // Build rope of "ab" alternating via insert-at-middle, then verify to_string.
    constexpr int N = 50;
    utf8_rope r;
    for (int i = 0; i < N; ++i) {
        const char* s = (i % 2 == 0) ? "a" : "b";
        r = r.insert(r.length() / 2, utf8_rope(s));
    }
    std::string result = r.to_string();
    ASSERT_EQ(result.size(), (size_t)N);
    // Count 'a' and 'b' — should be 25 each.
    EXPECT_EQ(std::count(result.begin(), result.end(), 'a'), 25);
    EXPECT_EQ(std::count(result.begin(), result.end(), 'b'), 25);
}

// ─── H. Rope erase/split/concat round-trip ────────────────────────────────────

TEST(PerfCorrectness, Rope_EraseAndRebuild) {
    // Build "abcde" * 10 = 50 cps.
    utf8_rope base;
    for (int i = 0; i < 10; ++i) base += utf8_rope("abcde");

    // Erase middle 10 cps (positions 20-29).
    utf8_rope erased = base.erase(20, 10);
    ASSERT_EQ(erased.length(), 40u);

    // Build expected string manually.
    std::string repeated;
    for (int i = 0; i < 10; ++i) repeated += "abcde";
    // Remove chars at byte positions 20-29 (all ASCII, so 1 byte per cp).
    std::string expected = repeated.substr(0, 20) + repeated.substr(30);

    EXPECT_EQ(erased.to_string(), expected);
}

TEST(PerfCorrectness, Rope_SplitConcatInvariant) {
    // split(pos) then concat should reproduce original.
    utf8_rope r;
    for (int i = 0; i < 20; ++i) r += utf8_rope("abc");
    std::string original = r.to_string();

    for (size_t pos = 0; pos <= 60; pos += 10) {
        auto [left, right] = r.split(pos);
        utf8_rope rejoined = left + right;
        EXPECT_EQ(rejoined.to_string(), original) << "split at pos=" << pos;
        EXPECT_EQ(rejoined.length(), r.length()) << "split at pos=" << pos;
    }
}

// ─── I. validate_utf8 exhaustive boundaries ──────────────────────────────────

TEST(PerfCorrectness, ValidateUtf8_AllSingleByteValues) {
    // Bytes 0x00-0x7F are valid single-byte sequences.
    for (int b = 0x01; b <= 0x7F; ++b) {
        char buf[2] = { static_cast<char>(b), 0 };
        EXPECT_TRUE(utf8_string::validate_utf8(std::string_view(buf, 1)))
            << "byte 0x" << std::hex << b;
    }
    // 0x80-0xBF are invalid as lead bytes.
    for (int b = 0x80; b <= 0xBF; ++b) {
        char buf[1] = { static_cast<char>(b) };
        EXPECT_FALSE(utf8_string::validate_utf8(std::string_view(buf, 1)))
            << "byte 0x" << std::hex << b;
    }
}

TEST(PerfCorrectness, ValidateUtf8_GoodMultibyte) {
    // A carefully crafted valid mixed sequence.
    const std::string valid_mixed =
        "ABC"                         // ASCII
        "\xC3\xA9"                    // U+00E9 é (2-byte)
        "\xE4\xB8\xAD"                // U+4E2D 中 (3-byte)
        "\xF0\x9F\x98\x80"            // U+1F600 😀 (4-byte)
        "XYZ";
    EXPECT_TRUE(utf8_string::validate_utf8(valid_mixed));
}

TEST(PerfCorrectness, ValidateUtf8_InvalidMidSequence) {
    // Insert a bad byte in the middle of a valid long string.
    std::string s(1000, 'A');
    s[500] = static_cast<char>(0xFE); // invalid UTF-8 byte
    EXPECT_FALSE(utf8_string::validate_utf8(s));
}

// ─── J. Large-string index fragmentation ─────────────────────────────────────

TEST(PerfCorrectness, LargeString_FragmentTransitions) {
    // Build string of exactly 3 * kCodePointsPerFragment = 180 code-points.
    // Verify that the fragment boundary code-points are indexed correctly.
    constexpr size_t kCPF = 60; // kCodePointsPerFragment
    constexpr size_t N = kCPF * 3;
    std::vector<uint32_t> cps = gen_mixed_cps(N);
    utf8_string s = from_cps(cps);
    ASSERT_EQ(s.length(), N);

    // Specifically check at and around each fragment boundary.
    for (size_t boundary : {kCPF - 1, kCPF, kCPF + 1,
                             2*kCPF - 1, 2*kCPF, 2*kCPF + 1}) {
        EXPECT_EQ(s[boundary], cps[boundary]) << "boundary=" << boundary;
    }
}

TEST(PerfCorrectness, LargeString_BatchDecode_FragmentTransitions) {
    constexpr size_t N = 180;
    std::vector<uint32_t> cps = gen_mixed_cps(N);
    utf8_string s = from_cps(cps);

    std::vector<uint32_t> batch(N);
    s.decode_code_points(0, N, batch.data());
    ASSERT_EQ(batch, cps);
}

// ─── K. Iterator arithmetic on mixed-width strings ────────────────────────────

TEST(PerfCorrectness, Iterator_RandomAccess_MixedWidth) {
    std::vector<uint32_t> cps = gen_mixed_cps(100);
    utf8_string s = from_cps(cps);

    auto it = s.begin();
    // += 50 should land on cp[50]
    it += 50;
    EXPECT_EQ(*it, cps[50]);

    // -= 10 should land on cp[40]
    it -= 10;
    EXPECT_EQ(*it, cps[40]);

    // operator+ / operator-
    auto it2 = it + 15;
    EXPECT_EQ(*it2, cps[55]);

    auto it3 = it2 - 5;
    EXPECT_EQ(*it3, cps[50]);

    // difference
    EXPECT_EQ(it2 - it3, 5);
}

TEST(PerfCorrectness, Iterator_PrePost_Decrement) {
    std::vector<uint32_t> cps = gen_mixed_cps(10);
    utf8_string s = from_cps(cps);

    auto it = s.end();
    --it; // pre-decrement
    EXPECT_EQ(*it, cps[9]);
    it--;  // post-decrement
    EXPECT_EQ(*it, cps[8]);
}

TEST(PerfCorrectness, Iterator_ForwardBackward_Consistency) {
    std::vector<uint32_t> cps = gen_mixed_cps(50);
    utf8_string s = from_cps(cps);

    // Walk forward, collect, walk back via -= from each position.
    auto it = s.begin();
    for (size_t i = 0; i < 50; ++i, ++it) {
        EXPECT_EQ(*it, cps[i]) << "forward i=" << i;
    }
    // Walk backward via --
    --it; // now at index 49
    for (int i = 49; i >= 0; --i, --it) {
        EXPECT_EQ(*it, cps[i]) << "backward i=" << i;
        if (i == 0) break;
    }
}

// ─── L. Move construct/assign preserves content ───────────────────────────────

TEST(PerfCorrectness, MoveConstruct_PreservesContent) {
    std::vector<uint32_t> cps = gen_mixed_cps(120);
    utf8_string s1 = from_cps(cps);
    utf8_string s2 = std::move(s1);
    ASSERT_EQ(s2.length(), 120u);
    for (size_t i = 0; i < 120; ++i) EXPECT_EQ(s2[i], cps[i]);
    // s1 should be in a valid (empty or consistent) state.
    EXPECT_EQ(s1.length(), 0u);
}

TEST(PerfCorrectness, MoveAssign_PreservesContent) {
    std::vector<uint32_t> cps = gen_mixed_cps(120);
    utf8_string s1 = from_cps(cps);
    utf8_string s2("temporary");
    s2 = std::move(s1);
    ASSERT_EQ(s2.length(), 120u);
    for (size_t i = 0; i < 120; ++i) EXPECT_EQ(s2[i], cps[i]);
}

TEST(PerfCorrectness, CopyConstruct_IndependentAfterModify) {
    std::vector<uint32_t> cps = gen_mixed_cps(30);
    utf8_string s1 = from_cps(cps);
    utf8_string s2 = s1; // copy
    // Modify s2 — s1 should be unaffected.
    s2.push_back('Z');
    EXPECT_EQ(s1.length(), 30u);
    EXPECT_EQ(s2.length(), 31u);
    for (size_t i = 0; i < 30; ++i) EXPECT_EQ(s1[i], cps[i]);
    EXPECT_EQ(s2[30], (uint32_t)'Z');
}

// ─── M. Rope: utf8_string integration ────────────────────────────────────────

TEST(PerfCorrectness, Rope_ToUtf8String_MatchesManualCollect) {
    utf8_rope r;
    r += utf8_rope("Hello ");
    r += utf8_rope("\xE4\xB8\xAD"); // 中
    r += utf8_rope(" World");
    r += utf8_rope("\xF0\x9F\x98\x80"); // 😀

    utf8_string via_method = r.to_utf8_string();
    utf8_string via_str    = utf8_string(r.to_string());

    EXPECT_EQ(via_method.length(), via_str.length());
    EXPECT_EQ(via_method.internal_data(), via_str.internal_data());
}

TEST(PerfCorrectness, Rope_Insert_MaintainsLength) {
    utf8_rope r("abcde");
    EXPECT_EQ(r.length(), 5u);
    r = r.insert(2, utf8_rope("XY"));
    EXPECT_EQ(r.length(), 7u);
    EXPECT_EQ(r.to_string(), "abXYcde");
}

TEST(PerfCorrectness, Rope_Find_MultiByte) {
    utf8_rope r("Hello \xE4\xB8\xAD World \xF0\x9F\x98\x80 done");
    // Find CJK character at code-point position 6.
    utf8_string needle("\xE4\xB8\xAD");
    EXPECT_EQ(r.find(needle), 6u);
    // Find emoji at code-point position 14.
    utf8_string emoji("\xF0\x9F\x98\x80");
    EXPECT_EQ(r.find(emoji), 14u);
}
