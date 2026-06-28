#pragma once

#include "task.h"
#include <mutex>
#include <cassert>

namespace wsp {

// Intrusive doubly-linked deque for per-worker task storage.
//
// Concurrency contract:
//   - Only the owning worker may call push_back() and pop_back() (LIFO end).
//   - Any thread may call steal_front() (FIFO end), which acquires the lock.
//   - push_back/pop_back also acquire the lock to safely coordinate with stealers.
//
// This is the "fine-grained lock" variant. The lock protects the front pointer
// against concurrent steals, while also serializing owner operations to prevent
// races when a steal and a pop_back target the same (last) element.
class WorkStealingDeque {
public:
    WorkStealingDeque() = default;
    ~WorkStealingDeque() = default;

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    // Owner pushes onto the back (LIFO end).
    void push_back(Task* t) {
        assert(t != nullptr);
        std::lock_guard<std::mutex> lk(mu_);
        t->next = nullptr;
        t->prev = back_;
        if (back_) back_->next = t;
        else       front_ = t;
        back_ = t;
        ++size_;
    }

    // Owner pops from the back (LIFO).
    Task* pop_back() {
        std::lock_guard<std::mutex> lk(mu_);
        if (!back_) return nullptr;
        Task* t = back_;
        back_ = t->prev;
        if (back_) back_->next = nullptr;
        else       front_ = nullptr;
        t->prev = t->next = nullptr;
        --size_;
        return t;
    }

    // Thief steals from the front (FIFO — opposite end from owner).
    Task* steal_front() {
        std::lock_guard<std::mutex> lk(mu_);
        if (!front_) return nullptr;
        Task* t = front_;
        front_ = t->next;
        if (front_) front_->prev = nullptr;
        else        back_ = nullptr;
        t->prev = t->next = nullptr;
        --size_;
        return t;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return size_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(mu_);
        return size_ == 0;
    }

private:
    mutable std::mutex mu_;
    Task* front_ = nullptr;
    Task* back_ = nullptr;
    size_t size_ = 0;
};

} // namespace wsp
