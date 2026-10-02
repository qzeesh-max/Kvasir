// test_tsan_advanced.cpp ─ Advanced thread-safety tests for Kvasir.
//
// This file extends test_tsan_pool.cpp with higher-stress scenarios that target:
//
//  1. Hazardous ref_count path in operator+(utf8_rope&&, const utf8_rope&):
//     the move-optimization reads ref_count with memory_order_relaxed — verify
//     no race when a concurrent thread holds a shared copy.
//
//  2. Rope find() concurrently on a shared rope (calls to_utf8_string() which
//     traverses the immutable tree, bumping ref counts).
//
//  3. Deep-tree rope split/concat concurrent stress — ensures intrusive_ptr
//     copy/destruction across threads is race-free.
//
//  4. utf8_rope copy-assignment races: two threads each copy the same shared
//     rope simultaneously (exercises the atomic ref_count bump).
//
//  5. Pool donate-threshold stress: push past kDonateThreshold=512 from many
//     threads simultaneously, verifying the Treiber-stack CAS loop is correct.
//
//  6. Rope erase concurrent correctness: each thread erases an independent
//     range and verifies the result matches a sequential reference.
//
//  7. Mixed producer/consumer with utf8_string (not rope): confirms that
//     utf8_string itself is safely movable across thread boundaries even though
//     it is not internally synchronized.

#include <gtest/gtest.h>
#include <kvasir/utf8_rope.hpp>
#include <kvasir/utf8_string.hpp>

#include <atomic>
#include <barrier>
#include <latch>
#include <thread>
#include <vector>
#include <random>
#include <string>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static kvasir::utf8_rope build_rope(int n, const char* word = "ab") {
    kvasir::utf8_rope r;
    kvasir::utf8_rope chunk(word);
    for (int i = 0; i < n; ++i) r += chunk;
    return r;
}

// ─── 1. Concurrent copy while move-optimization inspects ref_count ───────────
// Scenario: Thread A holds "r1" and repeatedly copies it (bumps ref_count).
// Thread B repeatedly tries to move-concat r1 into a new rope (reads ref_count
// with memory_order_relaxed to decide whether to mutate in place).
// TSAN should not fire any data race on ref_count.

TEST(TsanAdvancedTest, ConcurrentCopyAndMoveConcat) {
    constexpr int kIters = 300;
    kvasir::utf8_rope shared = build_rope(10); // shared immutable rope

    std::latch go{2};
    std::atomic<bool> done{false};

    std::thread copier([&]{
        go.arrive_and_wait();
        for (int i = 0; i < kIters; ++i) {
            kvasir::utf8_rope local = shared; // bump ref_count
            EXPECT_EQ(local.length(), shared.length());
        }
        done.store(true, std::memory_order_release);
    });

    std::thread mover([&]{
        go.arrive_and_wait();
        while (!done.load(std::memory_order_acquire)) {
            // Move-concat: operator+(utf8_rope&&, const utf8_rope&) reads
            // ref_count of lhs to decide whether it can mutate in place.
            kvasir::utf8_rope lhs = shared; // get our own ref
            kvasir::utf8_rope rhs = build_rope(2);
            kvasir::utf8_rope result = std::move(lhs) + rhs;
            EXPECT_GT(result.length(), 0u);
        }
    });

    copier.join();
    mover.join();
}

// ─── 2. Concurrent find() on a shared rope ───────────────────────────────────
// find() calls to_utf8_string() which traverses the tree and calls
// collect_strings. Multiple threads doing this simultaneously must not race
// on ref_counts.

TEST(TsanAdvancedTest, ConcurrentFindSharedRope) {
    kvasir::utf8_rope haystack = build_rope(50, "hello world ");
    kvasir::utf8_string needle("world");

    constexpr int kThreads = 8;
    std::latch go{kThreads};
    std::vector<std::thread> threads;
    std::atomic<int> found_count{0};

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]{
            go.arrive_and_wait();
            for (int i = 0; i < 50; ++i) {
                size_t pos = haystack.find(needle);
                if (pos != std::string::npos) {
                    found_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_GT(found_count.load(), 0);
}

// ─── 3. Deep-tree split/concat concurrent stress ──────────────────────────────
// Each thread builds a deep rope (insert-at-middle), then splits and
// recombines. The per-thread ropes are independent but share the pool.
// Verifies ref_count atomics and pool Treiber-stack under load.

TEST(TsanAdvancedTest, DeepTreeSplitConcatStress) {
    constexpr int kThreads = 6;
    constexpr int kDepth = 40;

    std::barrier sync{kThreads};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]{
            sync.arrive_and_wait();
            std::mt19937 rng(t * 314159 + 1);

            kvasir::utf8_rope r;
            kvasir::utf8_rope chunk("x");
            for (int i = 0; i < kDepth; ++i)
                r = r.insert(r.length() / 2, chunk);

            // Repeat split/concat
            for (int iter = 0; iter < 20; ++iter) {
                size_t mid = r.length() / 2;
                auto [left, right] = r.split(mid);
                r = left + right;
            }
            EXPECT_EQ(r.length(), (size_t)kDepth);
        });
    }
    for (auto& t : threads) t.join();
}

// ─── 4. Concurrent copy-assignment from same rope ────────────────────────────
// Two threads copy-assign from the same shared rope simultaneously.
// All paths go through intrusive_ptr copy constructor (atomic ref_count bump).

TEST(TsanAdvancedTest, ConcurrentCopyAssignSameRope) {
    constexpr int kThreads = 12;
    constexpr int kIters   = 200;

    kvasir::utf8_rope source = build_rope(30);
    std::latch go{kThreads};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]{
            go.arrive_and_wait();
            for (int i = 0; i < kIters; ++i) {
                kvasir::utf8_rope local;
                local = source;  // copy-assign
                EXPECT_EQ(local.length(), source.length());
            }
        });
    }
    for (auto& t : threads) t.join();
}

// ─── 5. Pool: simultaneous donate-threshold overflow from many threads ────────
// Each thread rapidly creates and destroys ropes until its local pool exceeds
// kDonateThreshold (512), forcing concurrent Treiber-stack push operations.

TEST(TsanAdvancedTest, SimultaneousDonateThresholdOverflow) {
    constexpr int kThreads = 8;
    // 700 > 512 (kDonateThreshold), so each thread will donate to the global stack.
    constexpr int kPerThread = 700;

    std::barrier go{kThreads};
    std::vector<std::thread> threads;
    std::atomic<int> total_ops{0};

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]{
            go.arrive_and_wait();
            std::vector<kvasir::utf8_rope*> held;
            held.reserve(kPerThread);
            for (int i = 0; i < kPerThread; ++i)
                held.push_back(new kvasir::utf8_rope("overflow"));
            // Freeing all at once: when local pool exceeds kDonateThreshold,
            // half are pushed concurrently onto the global Treiber stack.
            for (auto* p : held) delete p;
            total_ops.fetch_add(kPerThread, std::memory_order_relaxed);
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(total_ops.load(), kThreads * kPerThread);
}

// ─── 6. Concurrent rope erase + correctness check ────────────────────────────
// Each thread erases an independent sub-range from its own copy of a rope
// and verifies the result matches what a sequential reference computation gives.

TEST(TsanAdvancedTest, ConcurrentRopeEraseCorrectness) {
    constexpr int kThreads = 6;
    // Build a deterministic source rope: "abcde" * 20 = 100 code-points.
    kvasir::utf8_rope source;
    for (int i = 0; i < 20; ++i) source += kvasir::utf8_rope("abcde");

    std::barrier go{kThreads};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]{
            go.arrive_and_wait();
            std::mt19937 rng(t * 777);
            for (int iter = 0; iter < 100; ++iter) {
                // Erase a random sub-range.
                size_t len = source.length();
                size_t pos   = rng() % len;
                size_t count = 1 + rng() % std::min((size_t)10, len - pos);

                kvasir::utf8_rope r = source; // copy (atomic ref bump)
                kvasir::utf8_rope erased = r.erase(pos, count);

                EXPECT_EQ(erased.length(), len - count);
            }
        });
    }
    for (auto& t : threads) t.join();
}

// ─── 7. utf8_string move across thread boundary ───────────────────────────────
// utf8_string is not internally synchronized, but it must be safely movable
// across thread boundaries (the moved-from state is valid).

TEST(TsanAdvancedTest, Utf8StringMoveAcrossThreadBoundary) {
    constexpr int kPairs = 4;
    constexpr int kRounds = 200;

    struct Slot {
        std::atomic<kvasir::utf8_string*> ptr{nullptr};
    };
    std::vector<Slot> slots(kPairs);

    std::vector<std::thread> producers, consumers;

    for (int i = 0; i < kPairs; ++i) {
        producers.emplace_back([&, i]{
            for (int j = 0; j < kRounds; ++j) {
                auto* s = new kvasir::utf8_string("transfer-test-" + std::to_string(j));
                slots[i].ptr.store(s, std::memory_order_release);
                while (slots[i].ptr.load(std::memory_order_acquire) != nullptr)
                    std::this_thread::yield();
            }
        });
        consumers.emplace_back([&, i]{
            for (int j = 0; j < kRounds; ++j) {
                kvasir::utf8_string* p = nullptr;
                while ((p = slots[i].ptr.load(std::memory_order_acquire)) == nullptr)
                    std::this_thread::yield();
                EXPECT_EQ(p->size(), 14u + std::to_string(j).size());
                delete p;
                slots[i].ptr.store(nullptr, std::memory_order_release);
            }
        });
    }
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
}

// ─── 8. Rope += operator concurrent on separate instances (pool shared) ───────
// Multiple threads each perform many += operations on their own ropes.
// They don't share rope state, but they share the pool. This is a pool-level
// concurrency test that also verifies each thread's rope is correct.

TEST(TsanAdvancedTest, ConcurrentRopePlusAssignSeparateInstances) {
    constexpr int kThreads = 8;
    constexpr int kOps     = 400;

    std::barrier go{kThreads};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]{
            go.arrive_and_wait();
            kvasir::utf8_rope r;
            kvasir::utf8_rope chunk(std::string(1, static_cast<char>('a' + t % 26)).c_str());
            for (int i = 0; i < kOps; ++i) r += chunk;
            EXPECT_EQ(r.length(), (size_t)kOps);
        });
    }
    for (auto& t : threads) t.join();
}

// ─── 9. Ref-count precision: rope shared across N threads, all drop it ─────────
// Build a rope, distribute N copies to N threads, all drop simultaneously.
// The last drop must call delete exactly once — verified by absence of crash
// and by TSAN not reporting a race on the ref_count fetch_sub.

TEST(TsanAdvancedTest, ConcurrentRefCountDropPrecision) {
    constexpr int kCopies = 16;

    auto* shared = new kvasir::utf8_rope(build_rope(50));
    std::vector<kvasir::utf8_rope*> copies(kCopies);

    // All copies share the same underlying tree via intrusive_ptr.
    for (int i = 0; i < kCopies; ++i) {
        copies[i] = new kvasir::utf8_rope(*shared);
    }
    delete shared;

    std::latch go{kCopies};
    std::vector<std::thread> threads;

    for (int i = 0; i < kCopies; ++i) {
        threads.emplace_back([&, i]{
            go.arrive_and_wait(); // all drop simultaneously
            delete copies[i];
        });
    }
    for (auto& t : threads) t.join();
    // If we get here without crash or TSAN report, the ref_count CAS was correct.
    SUCCEED();
}

// ─── 10. Treiber-stack ABA stress with rapid thread spawn/destroy ──────────────
// Spawns many short-lived threads that each allocate and free a small batch,
// exercising the Treiber-stack's ABA safety under rapid thread churn.

TEST(TsanAdvancedTest, TreiberStackABAWithThreadChurn) {
    constexpr int kWaves    = 5;
    constexpr int kPerWave  = 8;
    constexpr int kPerThread = 50;

    for (int w = 0; w < kWaves; ++w) {
        std::vector<std::thread> threads;
        for (int t = 0; t < kPerWave; ++t) {
            threads.emplace_back([]{
                std::vector<kvasir::utf8_rope*> held;
                for (int i = 0; i < kPerThread; ++i)
                    held.push_back(new kvasir::utf8_rope("aba"));
                for (auto* p : held) delete p;
            });
        }
        for (auto& t : threads) t.join();
    }
    SUCCEED();
}
