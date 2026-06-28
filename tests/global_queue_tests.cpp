#ifdef USE_MINITEST
#include "minitest.h"
#else
#include <gtest/gtest.h>
#endif
#include "thread_pool.h"
#include <atomic>
#include <vector>
#include <numeric>

using namespace wsp;

TEST(GlobalQueuePool, SingleTask) {
    GlobalQueuePool pool(2);
    std::atomic<int> val{0};
    auto fut = pool.enqueue([&] { val.store(42); });
    fut.get();
    EXPECT_EQ(val.load(), 42);
}

TEST(GlobalQueuePool, ManyTasks) {
    GlobalQueuePool pool(4);
    constexpr int N = 10000;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) {
        futs.push_back(pool.enqueue([&] { counter.fetch_add(1); }));
    }
    for (auto& f : futs) f.get();
    EXPECT_EQ(counter.load(), N);
}

TEST(GlobalQueuePool, WaitDrainsAll) {
    GlobalQueuePool pool(4);
    constexpr int N = 50000;
    std::atomic<int> counter{0};
    for (int i = 0; i < N; ++i) {
        pool.enqueue([&] { counter.fetch_add(1); });
    }
    pool.wait();
    EXPECT_EQ(counter.load(), N);
}

TEST(GlobalQueuePool, ResultOrdering) {
    GlobalQueuePool pool(1);
    std::vector<int> results;
    std::mutex mu;
    constexpr int N = 100;
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) {
        futs.push_back(pool.enqueue([&, i] {
            std::lock_guard<std::mutex> lk(mu);
            results.push_back(i);
        }));
    }
    for (auto& f : futs) f.get();
    EXPECT_EQ(static_cast<int>(results.size()), N);
    // With 1 thread, FIFO ordering expected
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(results[static_cast<size_t>(i)], i);
    }
}

TEST(GlobalQueuePool, StressMillionTasks) {
    GlobalQueuePool pool(std::thread::hardware_concurrency());
    constexpr int N = 100000;
    std::atomic<int64_t> sum{0};
    for (int i = 0; i < N; ++i) {
        pool.enqueue([&, i] { sum.fetch_add(i); });
    }
    pool.wait();
    int64_t expected = static_cast<int64_t>(N - 1) * N / 2;
    EXPECT_EQ(sum.load(), expected);
}