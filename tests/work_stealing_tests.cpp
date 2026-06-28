#ifdef USE_MINITEST
#include "minitest.h"
#else
#include <gtest/gtest.h>
#endif
#include "thread_pool.h"
#include "work_stealing_deque.h"
#include <atomic>
#include <vector>
#include <set>
#include <thread>

using namespace wsp;

// --- Deque unit tests ---

TEST(WorkStealingDeque, PushPopBack) {
    WorkStealingDeque dq;
    Task t1, t2, t3;
    dq.push_back(&t1);
    dq.push_back(&t2);
    dq.push_back(&t3);
    EXPECT_EQ(dq.size(), 3u);
    EXPECT_EQ(dq.pop_back(), &t3);
    EXPECT_EQ(dq.pop_back(), &t2);
    EXPECT_EQ(dq.pop_back(), &t1);
    EXPECT_EQ(dq.pop_back(), nullptr);
}

TEST(WorkStealingDeque, StealFront) {
    WorkStealingDeque dq;
    Task t1, t2, t3;
    dq.push_back(&t1);
    dq.push_back(&t2);
    dq.push_back(&t3);
    EXPECT_EQ(dq.steal_front(), &t1);
    EXPECT_EQ(dq.steal_front(), &t2);
    EXPECT_EQ(dq.steal_front(), &t3);
    EXPECT_EQ(dq.steal_front(), nullptr);
}

TEST(WorkStealingDeque, MixedOwnerAndThief) {
    WorkStealingDeque dq;
    Task t1, t2, t3, t4;
    dq.push_back(&t1);
    dq.push_back(&t2);
    dq.push_back(&t3);
    dq.push_back(&t4);
    EXPECT_EQ(dq.steal_front(), &t1); // thief gets oldest
    EXPECT_EQ(dq.pop_back(), &t4);    // owner gets newest
    EXPECT_EQ(dq.size(), 2u);
}

TEST(WorkStealingDeque, ConcurrentSteal) {
    WorkStealingDeque dq;
    constexpr int N = 10000;
    std::vector<Task> tasks(N);
    for (int i = 0; i < N; ++i) dq.push_back(&tasks[static_cast<size_t>(i)]);

    std::atomic<int> stolen{0};
    std::vector<std::thread> thieves;
    for (int t = 0; t < 4; ++t) {
        thieves.emplace_back([&] {
            while (dq.steal_front() != nullptr) {
                stolen.fetch_add(1);
            }
        });
    }
    for (auto& th : thieves) th.join();
    EXPECT_EQ(stolen.load(), N);
}

// --- WorkStealingPool tests ---

TEST(WorkStealingPool, SingleTask) {
    WorkStealingPool pool(2);
    std::atomic<int> val{0};
    auto fut = pool.enqueue([&] { val.store(42); });
    fut.get();
    EXPECT_EQ(val.load(), 42);
}

TEST(WorkStealingPool, NoTaskLost) {
    WorkStealingPool pool(4);
    constexpr int N = 50000;
    std::atomic<int> counter{0};
    for (int i = 0; i < N; ++i) {
        pool.enqueue([&] { counter.fetch_add(1); });
    }
    pool.wait();
    EXPECT_EQ(counter.load(), N);
}

TEST(WorkStealingPool, NoTaskDuplicated) {
    WorkStealingPool pool(4);
    constexpr int N = 10000;
    std::atomic<int64_t> sum{0};
    for (int i = 0; i < N; ++i) {
        pool.enqueue([&, i] { sum.fetch_add(i); });
    }
    pool.wait();
    int64_t expected = static_cast<int64_t>(N - 1) * N / 2;
    EXPECT_EQ(sum.load(), expected);
}

TEST(WorkStealingPool, UnevenDistribution) {
    // Submit many tasks that vary in cost to force stealing
    WorkStealingPool pool(4);
    constexpr int N = 1000;
    std::atomic<int> counter{0};
    for (int i = 0; i < N; ++i) {
        pool.enqueue([&, i] {
            // Variable work: some tasks heavier than others
            volatile int x = 0;
            for (int j = 0; j < (i % 50); ++j) ++x;
            counter.fetch_add(1);
        });
    }
    pool.wait();
    EXPECT_EQ(counter.load(), N);
}

TEST(WorkStealingPool, StressTest) {
    WorkStealingPool pool(std::thread::hardware_concurrency());
    constexpr int N = 200000;
    std::atomic<int64_t> sum{0};
    for (int i = 0; i < N; ++i) {
        pool.enqueue([&, i] { sum.fetch_add(i); });
    }
    pool.wait();
    int64_t expected = static_cast<int64_t>(N - 1) * N / 2;
    EXPECT_EQ(sum.load(), expected);
}
