#pragma once

#include "task.h"
#include "global_queue.h"
#include "work_stealing_deque.h"

#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <random>
#include <cassert>
#include <memory>
#include <deque>

namespace wsp {

// Phase 0: simple thread pool backed only by a global MPMC queue.
// Uses a single mutex for both the task list and the condition variable
// to avoid missed wakeups.
class GlobalQueuePool {
public:
    explicit GlobalQueuePool(size_t num_threads = std::thread::hardware_concurrency())
        : pending_(0), stop_(false) {
        if (num_threads == 0) num_threads = 1;
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~GlobalQueuePool() { shutdown(); }

    GlobalQueuePool(const GlobalQueuePool&) = delete;
    GlobalQueuePool& operator=(const GlobalQueuePool&) = delete;

    template <typename F>
    std::future<void> enqueue(F&& fn) {
        auto promise = std::make_shared<std::promise<void>>();
        auto fut = promise->get_future();
        auto* t = new Task([f = std::forward<F>(fn), promise]() mutable {
            f();
            promise->set_value();
        });
        pending_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(mu_);
            tasks_.push_back(t);
        }
        cv_.notify_one();
        return fut;
    }

    void wait() {
        std::unique_lock<std::mutex> lk(done_mu_);
        done_cv_.wait(lk, [this] { return pending_.load(std::memory_order_acquire) == 0; });
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

private:
    void worker_loop() {
        for (;;) {
            Task* t = nullptr;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                if (!tasks_.empty()) {
                    t = tasks_.front();
                    tasks_.pop_front();
                }
            }
            if (t) {
                t->work();
                delete t;
                if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    done_cv_.notify_all();
                }
            }
        }
    }

    std::deque<Task*> tasks_;
    std::vector<std::thread> workers_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<int64_t> pending_;
    std::atomic<bool> stop_;
    std::mutex done_mu_;
    std::condition_variable done_cv_;
};

// Phase 1: work-stealing thread pool with per-worker deques.
class WorkStealingPool {
public:
    explicit WorkStealingPool(size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads == 0 ? 1 : num_threads),
          pending_(0), stop_(false), next_worker_(0) {
        deques_.reserve(num_threads_);
        for (size_t i = 0; i < num_threads_; ++i) {
            deques_.push_back(std::make_unique<WorkStealingDeque>());
        }
        for (size_t i = 0; i < num_threads_; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    ~WorkStealingPool() { shutdown(); }

    WorkStealingPool(const WorkStealingPool&) = delete;
    WorkStealingPool& operator=(const WorkStealingPool&) = delete;

    template <typename F>
    std::future<void> enqueue(F&& fn) {
        auto promise = std::make_shared<std::promise<void>>();
        auto fut = promise->get_future();
        auto* t = new Task([f = std::forward<F>(fn), promise]() mutable {
            f();
            promise->set_value();
        });
        pending_.fetch_add(1, std::memory_order_relaxed);

        size_t idx = next_worker_.fetch_add(1, std::memory_order_relaxed) % num_threads_;
        deques_[idx]->push_back(t);

        cv_.notify_all();
        return fut;
    }

    void wait() {
        std::unique_lock<std::mutex> lk(done_mu_);
        done_cv_.wait(lk, [this] { return pending_.load(std::memory_order_acquire) == 0; });
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    size_t num_workers() const { return num_threads_; }

private:
    void worker_loop(size_t id) {
        std::mt19937 rng(static_cast<unsigned>(id * 7919 + 42));

        for (;;) {
            Task* t = nullptr;

            // 1. Try own deque (LIFO)
            t = deques_[id]->pop_back();

            // 2. Try stealing from a random victim (FIFO)
            if (!t) {
                t = try_steal(id, rng);
            }

            // 3. Try the overflow queue
            if (!t) {
                t = overflow_.pop();
            }

            if (t) {
                t->work();
                delete t;
                if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    done_cv_.notify_all();
                }
                continue;
            }

            // No work found
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait_for(lk, std::chrono::microseconds(100), [this] {
                    return stop_.load(std::memory_order_relaxed);
                });
                if (stop_) {
                    drain_all(id, rng);
                    return;
                }
            }
        }
    }

    Task* try_steal(size_t my_id, std::mt19937& rng) {
        if (num_threads_ <= 1) return nullptr;
        std::uniform_int_distribution<size_t> dist(0, num_threads_ - 2);
        size_t victim = dist(rng);
        if (victim >= my_id) ++victim;
        return deques_[victim]->steal_front();
    }

    void drain_all(size_t id, std::mt19937& rng) {
        for (;;) {
            Task* t = deques_[id]->pop_back();
            if (!t) t = try_steal(id, rng);
            if (!t) t = overflow_.pop();
            if (!t) break;
            t->work();
            delete t;
            if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                done_cv_.notify_all();
            }
        }
    }

    size_t num_threads_;
    std::vector<std::unique_ptr<WorkStealingDeque>> deques_;
    GlobalQueue overflow_;
    std::vector<std::thread> workers_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<int64_t> pending_;
    std::atomic<bool> stop_;
    std::atomic<size_t> next_worker_;
    std::mutex done_mu_;
    std::condition_variable done_cv_;
};

} // namespace wsp
