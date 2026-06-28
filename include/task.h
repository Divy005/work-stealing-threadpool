#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <future>

namespace wsp {

struct Task {
    // Intrusive list pointers for the work-stealing deque
    Task* prev = nullptr;
    Task* next = nullptr;

    std::function<void()> work;

    Task() = default;
    explicit Task(std::function<void()> fn) : work(std::move(fn)) {}
};

} // namespace wsp
