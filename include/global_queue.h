#pragma once

#include "task.h"
#include <mutex>
#include <condition_variable>
#include <cassert>

namespace wsp {

// MPMC FIFO queue protected by a single mutex.
// Used as the overflow/fallback queue for external task submission.
class GlobalQueue {
public:
    GlobalQueue() = default;
    ~GlobalQueue() { clear(); }

    GlobalQueue(const GlobalQueue&) = delete;
    GlobalQueue& operator=(const GlobalQueue&) = delete;

    void push(Task* t) {
        assert(t != nullptr);
        std::lock_guard<std::mutex> lk(mu_);
        t->prev = nullptr;
        t->next = head_;
        if (head_) head_->prev = t;
        else       tail_ = t;
        head_ = t;
        ++size_;
    }

    // Pop from tail (FIFO: push at head, pop from tail)
    Task* pop() {
        std::lock_guard<std::mutex> lk(mu_);
        return pop_tail_locked();
    }

    bool empty() {
        std::lock_guard<std::mutex> lk(mu_);
        return size_ == 0;
    }

    size_t size() {
        std::lock_guard<std::mutex> lk(mu_);
        return size_;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mu_);
        while (pop_tail_locked()) {}
    }

private:
    Task* pop_tail_locked() {
        if (!tail_) return nullptr;
        Task* t = tail_;
        tail_ = t->next;
        if (tail_) tail_->prev = nullptr;
        else       head_ = nullptr;
        t->prev = t->next = nullptr;
        --size_;
        return t;
    }

    std::mutex mu_;
    Task* head_ = nullptr;
    Task* tail_ = nullptr;
    size_t size_ = 0;
};

} // namespace wsp
