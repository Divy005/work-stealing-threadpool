#include "thread_pool.h"
#include <chrono>
#include <iostream>
#include <atomic>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>

using Clock = std::chrono::high_resolution_clock;

struct BenchResult {
    double throughput_mtps; // million tasks per second
    double elapsed_ms;
    double p50_ms;
    double p99_ms;
};

template <typename Pool>
BenchResult run_bench(const std::string& name, size_t num_threads, int num_tasks, int runs = 5) {
    std::vector<double> elapsed_all;

    for (int r = 0; r < runs; ++r) {
        Pool pool(num_threads);
        std::atomic<int64_t> sum{0};

        auto start = Clock::now();
        for (int i = 0; i < num_tasks; ++i) {
            pool.enqueue([&, i] { sum.fetch_add(i); });
        }
        pool.wait();
        auto end = Clock::now();

        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        elapsed_all.push_back(ms);
    }

    std::sort(elapsed_all.begin(), elapsed_all.end());
    size_t n = elapsed_all.size();
    double p50 = elapsed_all[n / 2];
    double p99 = elapsed_all[static_cast<size_t>(n * 0.99)];
    double median = p50;
    double mtps = (num_tasks / 1e6) / (median / 1e3);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[" << name << "] threads=" << num_threads
              << " tasks=" << num_tasks
              << " | p50=" << p50 << "ms"
              << " p99=" << p99 << "ms"
              << " throughput=" << mtps << " Mtasks/s\n";

    return {mtps, median, p50, p99};
}

int main() {
    std::cout << "=== Work-Stealing Thread Pool Benchmarks ===\n\n";

    size_t hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;

    constexpr int TASKS_100K = 100000;
    constexpr int TASKS_500K = 500000;

    std::cout << "--- Phase 0: GlobalQueuePool (baseline) ---\n";
    auto gq_100k = run_bench<wsp::GlobalQueuePool>("GlobalQueue-100K", hw, TASKS_100K);
    auto gq_500k = run_bench<wsp::GlobalQueuePool>("GlobalQueue-500K", hw, TASKS_500K);

    std::cout << "\n--- Phase 1: WorkStealingPool ---\n";
    auto ws_100k = run_bench<wsp::WorkStealingPool>("WorkStealing-100K", hw, TASKS_100K);
    auto ws_500k = run_bench<wsp::WorkStealingPool>("WorkStealing-500K", hw, TASKS_500K);

    std::cout << "\n--- Scaling test (WorkStealingPool, 500K tasks) ---\n";
    for (size_t t = 1; t <= hw; ++t) {
        run_bench<wsp::WorkStealingPool>("WS-scale", t, TASKS_500K);
    }

    std::cout << "\n--- Comparison ---\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "100K tasks: WorkStealing/GlobalQueue speedup = "
              << ws_100k.throughput_mtps / gq_100k.throughput_mtps << "x\n";
    std::cout << "500K tasks: WorkStealing/GlobalQueue speedup = "
              << ws_500k.throughput_mtps / gq_500k.throughput_mtps << "x\n";

    return 0;
}
