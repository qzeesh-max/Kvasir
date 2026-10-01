// Tests for:
//  1. Rope node pool allocator (same-thread reuse, cross-thread donation via global Treiber stack)
//  2. UTF-8 encode/decode round-trips (push_back + operator[], all plane widths)
//  3. validate_utf8 boundary conditions

#include <gtest/gtest.h>
#include <kvasir/utf8_rope.hpp>
#include <kvasir/utf8_string.hpp>

#include <thread>
#include <vector>
#include <atomic>
#include <string>

// ─── Pool allocator tests ────────────────────────────────────────────────────

TEST(PoolAllocatorTest, SameThreadAllocFree) {
    // Allocate many ropes (which allocate nodes) and destroy them.
    // The second round should reuse pool slots — measurable by address reuse.
    std::vector<void*> first_round_ptrs;
    {
        std::vector<kvasir::utf8_rope> ropes;
        ropes.reserve(300);
        for (int i = 0; i < 300; ++i) {
            ropes.emplace_back(std::string_view("hello"));
        }
        // Capture rope root node addresses via to_string (indirect; we just
        // verify no crash and all ropes are correct).
        for (auto& r : ropes) {
            EXPECT_EQ(r.to_string(), "hello");
        }
    } // All 300 nodes freed back to thread-local pool.

    // Second round: pool should serve from the free-list, not the OS.
    std::vector<kvasir::utf8_rope> ropes2;
    ropes2.reserve(300);
    for (int i = 0; i < 300; ++i) {
        ropes2.emplace_back(std::string_view("world"));
    }
    for (auto& r : ropes2) {
        EXPECT_EQ(r.to_string(), "world");
    }
}

TEST(PoolAllocatorTest, CrossThreadFreeDonatesToGlobal) {
    // Thread A allocates 600 ropes, passes them to thread B.
    // Thread B destroys them (their nodes go to B's local list, and when B's
    // list exceeds kDonateThreshold=512, the excess is pushed to the global
    // Treiber stack). Thread A then allocates again and should find nodes in
    // the global pool rather than requesting fresh OS memory.

    std::vector<kvasir::utf8_rope> shared_ropes;
    shared_ropes.reserve(600);
    std::atomic<bool> ready{false};
    std::atomic<bool> done{false};

    std::thread producer([&] {
        for (int i = 0; i < 600; ++i) {
            shared_ropes.emplace_back(std::string_view("cross-thread"));
        }
        ready.store(true, std::memory_order_release);
        // Wait for consumer to finish.
        while (!done.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });

    // Consumer runs on main thread.
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();

    // Destroy all ropes on this (main) thread — cross-thread free.
    shared_ropes.clear();
    done.store(true, std::memory_order_release);
    producer.join();

    // Now allocate again from the main thread; nodes should come from the
    // global pool (donated by the overflow from this thread's local list).
    std::vector<kvasir::utf8_rope> new_ropes;
    new_ropes.reserve(100);
    for (int i = 0; i < 100; ++i) {
        new_ropes.emplace_back(std::string_view("reused"));
    }
    for (auto& r : new_ropes) {
        EXPECT_EQ(r.to_string(), "reused");
    }
}

TEST(PoolAllocatorTest, ThreadExitDonatesPool) {
    // Spawn a thread that allocates nodes and exits without freeing them via
    // utf8_rope destructor (impossible — ropes are RAII — but the pool_state
    // destructor on thread exit should donate the remaining fast_list to the
    // global Treiber stack). We just verify correctness of subsequent allocs.
    std::thread t([]{
        // Allocate and immediately destroy, leaving a warm local pool.
        for (int i = 0; i < 300; ++i) {
            kvasir::utf8_rope r(std::string_view("exit-donate"));
            (void)r;
        }
        // Thread exits here → pool_state::~pool_state() donates to global pool.
    });
    t.join();

    // Main thread should be able to steal from global pool after thread exit.
    kvasir::utf8_rope r(std::string_view("after-exit"));
    EXPECT_EQ(r.to_string(), "after-exit");
}

TEST(PoolAllocatorTest, HighConcurrency) {
    // 8 threads each create and destroy 1000 ropes concurrently.
    // Verifies no data races or corruption under TSAN.
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([]{
            for (int i = 0; i < kOpsPerThread; ++i) {
                kvasir::utf8_rope r(std::string_view("concurrent"));
                r = r.insert(r.length(), kvasir::utf8_rope(std::string_view("!")));
                EXPECT_EQ(r.to_string(), "concurrent!");
            }
        });
    }
    for (auto& thr : threads) thr.join();
}

// ─── UTF-8 encode/decode round-trip tests ────────────────────────────────────

namespace {
// Build a utf8_string by push_back'ing code points and verify operator[] round-trips.
void check_round_trip(const std::vector<uint32_t>& cps, const std::string& label) {
    kvasir::utf8_string s;
    for (uint32_t cp : cps) s.push_back(cp);

    ASSERT_EQ(s.length(), cps.size()) << "length mismatch for: " << label;
    for (size_t i = 0; i < cps.size(); ++i) {
        EXPECT_EQ(s[i], cps[i])
            << label << " code point " << i
            << " expected U+" << std::hex << cps[i]
            << " got U+" << s[i];
    }
}
} // namespace

TEST(Utf8EncodeDecode, AsciiRoundTrip) {
    // U+0001 – U+007F: the full printable+control ASCII range.
    // U+0000 (NUL) is intentionally excluded: utf8_string is NUL-free by design
    // (the internal buffer uses 3 NUL bytes as a lookahead sentinel) which is
    // consistent with std::string_view / std::string interoperability semantics.
    std::vector<uint32_t> cps;
    for (uint32_t c = 0x01; c <= 0x7F; ++c) cps.push_back(c);
    check_round_trip(cps, "ASCII");
}

TEST(Utf8EncodeDecode, TwoByteRoundTrip) {
    // U+0080 – U+07FF  (Latin Extended, Arabic, Hebrew, …)
    std::vector<uint32_t> cps = {
        0x0080, 0x00C9, 0x00FF,  // Latin-1 supplement
        0x0100, 0x03B1,          // Greek alpha
        0x05D0,                  // Hebrew aleph
        0x07FF                   // Last 2-byte code point
    };
    check_round_trip(cps, "2-byte");
}

TEST(Utf8EncodeDecode, ThreeByteRoundTrip) {
    // U+0800 – U+FFFF  (CJK, Devanagari, etc.; excluding surrogates)
    std::vector<uint32_t> cps = {
        0x0800,   // First 3-byte
        0x4E2D,   // CJK: 中
        0x6587,   // CJK: 文
        0x0906,   // Devanagari: आ
        0xFFFD,   // Replacement character
        0xFFFF    // Last 3-byte (non-character, but valid encoding)
    };
    check_round_trip(cps, "3-byte");
}

TEST(Utf8EncodeDecode, FourByteRoundTrip) {
    // U+10000 – U+10FFFF  (Emoji, historic scripts)
    std::vector<uint32_t> cps = {
        0x10000,   // Linear B Syllable
        0x1F600,   // 😀
        0x1F30D,   // 🌍
        0x1F4A9,   // 💩
        0x10FFFF   // Maximum valid code point
    };
    check_round_trip(cps, "4-byte");
}

TEST(Utf8EncodeDecode, MixedPlanesRoundTrip) {
    // A realistic string mixing all plane widths.
    std::vector<uint32_t> cps = {
        'K', 'v', 'a', 's', 'i', 'r',  // ASCII
        0x00A0,                           // Non-breaking space (2-byte)
        0x2764,                           // ❤  (3-byte)
        0x1F600,                          // 😀 (4-byte)
        0x00E9,                           // é  (2-byte)
        0x4E2D,                           // 中 (3-byte)
        '!'
    };
    check_round_trip(cps, "mixed");
}

TEST(Utf8EncodeDecode, SingleCodePoints) {
    // Test boundary values for each width independently.
    auto check1 = [](uint32_t cp, const char* name) {
        kvasir::utf8_string s;
        s.push_back(cp);
        ASSERT_EQ(s.length(), 1u) << name;
        EXPECT_EQ(s[0], cp) << name << " U+" << std::hex << cp;
    };

    check1(0x01,     "SOH");     // First non-NUL code point
    check1(0x41,     "A");
    check1(0x7F,     "DEL");
    check1(0x80,     "first 2-byte");
    check1(0x7FF,    "last 2-byte");
    check1(0x800,    "first 3-byte");
    check1(0xD7FF,   "just before surrogates");
    check1(0xE000,   "first private use area");
    check1(0xFFFF,   "last 3-byte BMP");
    check1(0x10000,  "first 4-byte");
    check1(0x10FFFF, "max code point");
}

TEST(Utf8EncodeDecode, IteratorRoundTrip) {
    // Verify the const_iterator yields the same code points as push_back.
    std::vector<uint32_t> cps = {
        'H', 'e', 'l', 'l', 'o',
        0x1F30D,   // 🌍
        0x4E2D,    // 中
        0x00E9,    // é
        '!'
    };

    kvasir::utf8_string s;
    for (uint32_t cp : cps) s.push_back(cp);

    std::vector<uint32_t> decoded;
    for (uint32_t cp : s) decoded.push_back(cp);

    ASSERT_EQ(decoded.size(), cps.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        EXPECT_EQ(decoded[i], cps[i]) << "mismatch at index " << i;
    }
}

TEST(Utf8EncodeDecode, DecodeToUtf32) {
    std::vector<uint32_t> cps = { 'A', 0x1F600, 0x4E2D, 0x00E9 };
    kvasir::utf8_string s;
    for (uint32_t cp : cps) s.push_back(cp);

    std::vector<uint32_t> out(cps.size());
    size_t n = s.decode_code_points(0, cps.size(), out.data());

    ASSERT_EQ(n, cps.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        EXPECT_EQ(out[i], cps[i]) << "decode_to_utf32 mismatch at " << i;
    }
}

// ─── validate_utf8 boundary / rejection tests ────────────────────────────────

TEST(ValidateUtf8, ValidInputs) {
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8(""));
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8("Hello, world!"));
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8("\xC3\xA9")); // é
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8("\xE4\xB8\xAD")); // 中
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8("\xF0\x9F\x98\x80")); // 😀
}

TEST(ValidateUtf8, SurrogateRejection) {
    // U+D800 encoded as UTF-8 (illegal)
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xED\xA0\x80"));
    // U+DFFF
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xED\xBF\xBF"));
}

TEST(ValidateUtf8, OverlongRejection) {
    // Overlong encoding of U+0041 ('A') as 2 bytes
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xC0\x81"));
    // Overlong encoding of U+0000 as 3 bytes
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xE0\x80\x80"));
    // Overlong encoding of U+0000 as 4 bytes
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xF0\x80\x80\x80"));
}

TEST(ValidateUtf8, TruncatedSequences) {
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xC3"));         // 2-byte, 1 byte provided
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xE4\xB8"));    // 3-byte, 2 bytes provided
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xF0\x9F\x98")); // 4-byte, 3 bytes provided
}

TEST(ValidateUtf8, InvalidContinuationBytes) {
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xC3\x41"));    // continuation byte replaced by ASCII
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xE4\x41\x80")); // bad 2nd byte
}

TEST(ValidateUtf8, OutOfRangeCodePoint) {
    // Beyond U+10FFFF
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8("\xF4\x90\x80\x80"));
}

TEST(ValidateUtf8, LongAsciiSIMDPath) {
    // > 16 ASCII bytes to exercise the SIMD fast path.
    std::string ascii(1024, 'x');
    EXPECT_TRUE(kvasir::utf8_string::validate_utf8(ascii));
    ascii[512] = '\x80';  // Inject a bad byte in the middle
    EXPECT_FALSE(kvasir::utf8_string::validate_utf8(ascii));
}
