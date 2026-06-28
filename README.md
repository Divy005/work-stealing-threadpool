# Work-Stealing Thread Pool Scheduler

A high-performance work-stealing thread pool in modern C++17, built entirely from first principles.
All synchronization primitives, scheduling logic, and data structures are hand-rolled — no TBB,
Boost.Asio, or off-the-shelf lock-free libraries.

## Project Status

- **Phase 0** ✅ — Global queue thread pool (baseline)
- **Phase 1** ✅ — Work-stealing with per-thread intrusive deques
- **Phase 2** 🔲 — Lock-free optimizations, batch stealing, backoff
- **Phase 3** 🔲 — Benchmarking suite, observability, documentation

## Build

```bash
# Standard build
mkdir build && cd build
cmake .. && make -j$(nproc)

# Run tests
./pool_tests

# Run benchmarks
./pool_bench

# Build with ThreadSanitizer
mkdir build-tsan && cd build-tsan
cmake -DENABLE_TSAN=ON .. && make -j$(nproc)
./pool_tests

# Build with AddressSanitizer
mkdir build-asan && cd build-asan
cmake -DENABLE_ASAN=ON .. && make -j$(nproc)
./pool_tests
```

## Architecture

```
Caller → enqueue() → round-robin to Worker[i].deque

Worker[i]:
  1. pop_back() own deque       (LIFO, low contention)
  2. steal_front() random peer  (FIFO, lock-protected)
  3. pop() overflow queue        (FIFO, fallback)
  4. wait on CV
```

Each worker owns an **intrusive doubly-linked deque**. Workers push/pop from the back (LIFO);
thieves steal from the front (FIFO). This gives temporal locality to the owner while balancing
load across idle workers.

## API

```cpp
#include "thread_pool.h"

// Work-stealing pool (Phase 1 — recommended)
wsp::WorkStealingPool pool(4); // 4 worker threads
pool.enqueue([] { /* your work */ });
pool.wait(); // block until all tasks complete

// Global queue pool (Phase 0 — baseline)
wsp::GlobalQueuePool baseline(4);
baseline.enqueue([] { /* your work */ });
baseline.wait();
```

## Key Design Decisions

- **Intrusive linked list deques** — no `std::deque`, no heap allocation for queue nodes
- **Per-deque mutex** — fine-grained locking, one lock per worker (not one global lock)
- **Random victim selection** — uniform random steal target to avoid hot-spotting
- **LIFO local / FIFO steal** — classic work-stealing for cache locality + fairness
- **Overflow queue** — ensures external submissions always succeed

See [DESIGN.md](DESIGN.md) for the full concurrency model and memory ordering rationale.

## Testing

- GoogleTest unit tests for both pool implementations and the deque
- Stress tests with 200K+ tasks verifying no drops or duplicates
- ThreadSanitizer gate (zero data races)
- AddressSanitizer gate (no memory errors)

## Directory Layout

```
include/
  task.h                  — Task abstraction (intrusive list node)
  global_queue.h          — MPMC FIFO overflow queue
  work_stealing_deque.h   — Per-worker intrusive deque
  thread_pool.h           — GlobalQueuePool + WorkStealingPool
tests/
  global_queue_tests.cpp  — Phase 0 tests
  work_stealing_tests.cpp — Phase 1 tests + deque unit tests
benchmarks/
  bench_main.cpp          — Throughput + scaling benchmarks
DESIGN.md                 — Architecture and concurrency model
```
