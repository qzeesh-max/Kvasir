// TSAN-targeted tests for utf8_rope's two-level lock-free pool allocator.
//
// Each test is designed to trigger the cross-thread code paths under
// ThreadSanitizer so that any real data races will be caught:
//
//   - Simultaneous alloc on N threads while others free
//   - Producer→consumer: thread A allocates, thread B destroys
//   - Flood the Treiber stack from many threads concurrently
//   - Thread death during active rope lifetime (pool_state destructor races)
//   - Round-robin producer/consumer chains
//   - Concurrent rope modification (insert/concat/split) from different threads

#include <gtest/gtest.h>
#include <kvasir/utf8_rope.hpp>
#include <kvasir/utf8_string.hpp>

#include <atomic>
#include <barrier>
#include <thread>
#include <vector>
#include <latch>
#include <random>

// ─── helpers ─────────────────────────────────────────────────────────────────

static kvasir::utf8_rope make_rope(int n) {
    kvasir::utf8_rope r;
    for (int i = 0; i < n; ++i)
        r = r.insert(r.length(), kvasir::utf8_rope("x"));
    return r;
}

// ─── 1. N producers / N consumers round-robin ────────────────────────────────

TEST(TsanPoolTest, ProducerConsumerRoundRobin) {
    constexpr int kThreadPairs = 4;
    constexpr int kRopes = 200;

    // Each pair: thread A allocates, stores in shared slot; thread B reads/destroys.
    struct Slot {
        std::atomic<kvasir::utf8_rope*> rope{nullptr};
    };

    std::vector<Slot> slots(kThreadPairs);
    std::vector<std::thread> producers, consumers;

    for (int i = 0; i < kThreadPairs; ++i) {
        producers.emplace_back([&, i]{
            for (int j = 0; j < kRopes; ++j) {
                auto* r = new kvasir::utf8_rope(std::string_view("hello-world"));
                // Publish with release semantics.
                slots[i].rope.store(r, std::memory_order_release);
                // Wait for consumer to clear it.
                while (slots[i].rope.load(std::memory_order_acquire) != nullptr)
                    std::this_thread::yield();
            }
        });

        consumers.emplace_back([&, i]{
            for (int j = 0; j < kRopes; ++j) {
                kvasir::utf8_rope* r = nullptr;
                while ((r = slots[i].rope.load(std::memory_order_acquire)) == nullptr)
                    std::this_thread::yield();
                EXPECT_EQ(r->to_string(), "hello-world");
                delete r;
                slots[i].rope.store(nullptr, std::memory_order_release);
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
}

// ─── 2. Parallel allocators racing on the global Treiber stack ───────────────
// One thread saturates its local pool past kDonateThreshold (512) to force
// donation to the global stack; concurrently other threads are popping from it.

TEST(TsanPoolTest, TreiberStackRace) {
    constexpr int kAllocThreads  = 3;
    constexpr int kStealsPerThread = 600; // exceeds kDonateThreshold

    std::latch start{kAllocThreads + 1};
    std::vector<std::thread> threads;
    std::atomic<int> total_created{0};

    for (int t = 0; t < kAllocThreads; ++t) {
        threads.emplace_back([&]{
            start.arrive_and_wait();
            std::vector<kvasir::utf8_rope*> held;
            held.reserve(kStealsPerThread);
            // Allocate many nodes — when count > 512, half go to global pool.
            for (int i = 0; i < kStealsPerThread; ++i) {
                held.push_back(new kvasir::utf8_rope(std::string_view("treiber")));
                total_created.fetch_add(1, std::memory_order_relaxed);
            }
            // Free them all — donate to global or local.
            for (auto* p : held) delete p;
        });
    }

    start.arrive_and_wait(); // release all threads simultaneously
    for (auto& t : threads) t.join();
    EXPECT_EQ(total_created.load(), kAllocThreads * kStealsPerThread);
}

// ─── 3. Thread-exit donation race ────────────────────────────────────────────
// Threads exit while the main thread is actively allocating from the global pool.

TEST(TsanPoolTest, ThreadExitDonationWhileAllocating) {
    constexpr int kExitThreads = 8;
    std::latch ready{kExitThreads};
    std::atomic<bool> go{false};

    std::vector<std::thread> exiters;
    for (int i = 0; i < kExitThreads; ++i) {
        exiters.emplace_back([&]{
            // Warm up a local pool.
            std::vector<kvasir::utf8_rope*> held;
            for (int j = 0; j < 300; ++j)
                held.push_back(new kvasir::utf8_rope(std::string_view("exiting")));
            for (auto* p : held) delete p;

            ready.count_down();
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();
            // Thread exits → pool_state::~pool_state() donates to Treiber stack.
        });
    }

    ready.wait(); // all have warm pools
    go.store(true, std::memory_order_release);

    // Concurrently allocate from main thread while exiters are dying.
    std::vector<kvasir::utf8_rope*> main_ropes;
    for (int i = 0; i < 200; ++i)
        main_ropes.push_back(new kvasir::utf8_rope(std::string_view("main")));

    for (auto& t : exiters) t.join();
    for (auto* p : main_ropes) delete p;
    SUCCEED();
}

// ─── 4. Concurrent rope mutation (insert/concat/erase) ───────────────────────
// Each thread operates on its own rope, but they share nothing — verifies that
// the pool's Treiber stack is ABA-safe under concurrent push/pop.

TEST(TsanPoolTest, ConcurrentRopeMutation) {
    constexpr int kThreads = 8;
    constexpr int kOps     = 300;

    std::barrier sync_point{kThreads};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]{
            sync_point.arrive_and_wait(); // all start together
            std::mt19937 rng(t * 1234567 + 42);
            std::uniform_int_distribution<int> op_dist(0, 2);

            kvasir::utf8_rope r(std::string_view("seed"));
            for (int i = 0; i < kOps; ++i) {
                switch (op_dist(rng)) {
                    case 0:
                        r = r.insert(r.length() / 2, kvasir::utf8_rope("ins"));
                        break;
                    case 1:
                        r = r + kvasir::utf8_rope("cat");
                        break;
                    case 2:
                        if (r.length() > 4)
                            r = r.erase(1, 2);
                        break;
                }
            }
            EXPECT_GT(r.length(), 0u);
        });
    }
    for (auto& t : threads) t.join();
}

// ─── 5. Many threads share a single rope (read-only) ─────────────────────────
// utf8_rope is immutable/copy-on-write via intrusive_ptr; concurrent reads of
// the same root node must be race-free on the ref_count atomic.

TEST(TsanPoolTest, SharedRopeReadConcurrently) {
    kvasir::utf8_rope shared = make_rope(64);
    constexpr int kReaders = 12;

    std::vector<std::thread> readers;
    std::latch go{kReaders};

    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&]{
            go.count_down();
            go.wait();
            // Read-only: copy (bumps ref_count), traverse, drop (decrements ref_count).
            for (int j = 0; j < 50; ++j) {
                kvasir::utf8_rope local_copy = shared; // atomic ref_count bump
                EXPECT_EQ(local_copy.length(), 64u);
                EXPECT_EQ(local_copy.to_string(), shared.to_string());
            }
        });
    }
    for (auto& t : readers) t.join();
}

// ─── 6. Pool: interleaved alloc/free across thread boundary ──────────────────
// Thread A allocates a batch; thread B frees it; thread A allocates again.
// Verifies the global Treiber stack correctly bridges the lifetime boundary.

TEST(TsanPoolTest, InterleavedAllocFreeCrossThread) {
    constexpr int kBatch = 400;
    std::vector<kvasir::utf8_rope*> batch;
    batch.reserve(kBatch);

    std::atomic<bool> a_done{false}, b_done{false};

    std::thread thread_a([&]{
        // Round 1: allocate.
        for (int i = 0; i < kBatch; ++i)
            batch.push_back(new kvasir::utf8_rope(std::string_view("A-alloc")));
        a_done.store(true, std::memory_order_release);
        // Wait for B to free them.
        while (!b_done.load(std::memory_order_acquire))
            std::this_thread::yield();
        // Round 2: re-allocate — should reclaim from global pool.
        std::vector<kvasir::utf8_rope*> round2;
        for (int i = 0; i < kBatch / 2; ++i)
            round2.push_back(new kvasir::utf8_rope(std::string_view("A-reuse")));
        for (auto* p : round2) {
            EXPECT_EQ(p->to_string(), "A-reuse");
            delete p;
        }
    });

    // Thread B waits for batch then frees it.
    while (!a_done.load(std::memory_order_acquire))
        std::this_thread::yield();
    for (auto* p : batch) delete p;
    batch.clear();
    b_done.store(true, std::memory_order_release);

    thread_a.join();
}

// ─── 7. Stress: many threads, random mix of alloc/free/rope-ops ──────────────

TEST(TsanPoolTest, StressRandomMix) {
    constexpr int kThreads = 6;
    constexpr int kIters   = 500;

    std::atomic<int> live_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]{
            std::mt19937 rng(t * 999983 + 7);
            std::uniform_int_distribution<int> op(0, 3);
            std::vector<kvasir::utf8_rope*> held;

            for (int i = 0; i < kIters; ++i) {
                switch (op(rng)) {
                    case 0: { // allocate
                        auto* r = new kvasir::utf8_rope(std::string_view("stress"));
                        held.push_back(r);
                        live_count.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    case 1: { // free one
                        if (!held.empty()) {
                            delete held.back();
                            held.pop_back();
                            live_count.fetch_sub(1, std::memory_order_relaxed);
                        }
                        break;
                    }
                    case 2: { // rope op on a held rope
                        if (!held.empty()) {
                            *held.back() = (*held.back()) + kvasir::utf8_rope("!");
                        }
                        break;
                    }
                    case 3: { // free all
                        live_count.fetch_sub(static_cast<int>(held.size()),
                                             std::memory_order_relaxed);
                        for (auto* p : held) delete p;
                        held.clear();
                        break;
                    }
                }
            }
            live_count.fetch_sub(static_cast<int>(held.size()),
                                 std::memory_order_relaxed);
            for (auto* p : held) delete p;
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(live_count.load(), 0);
}
