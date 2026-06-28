# Work-Stealing Thread Pool — Design Document

## Overview

A high-performance work-stealing thread pool scheduler in C++17, built from first principles.
No external concurrency libraries — all synchronization, scheduling, and data structures are
hand-rolled.

## Architecture

```
Caller → enqueue() → round-robin to Worker[i].deque
                   ↘ overflow GlobalQueue (fallback)

Worker[i]:
  1. pop_back() from own deque      (LIFO, low contention)
  2. steal_front() from random peer (FIFO, lock-protected)
  3. pop() from overflow queue       (FIFO, mutex-protected)
  4. wait on condition variable
```

## Data Structures

### Task (include/task.h)
- Intrusive doubly-linked list node with `prev`/`next` pointers
- Wraps a `std::function<void()>`
- Heap-allocated per submission, deleted after execution

### GlobalQueue (include/global_queue.h)
- MPMC FIFO queue using intrusive linked list
- Single `std::mutex` protects all operations
- Push at head, pop from tail → FIFO ordering
- Used as overflow fallback and as the Phase 0 primary queue

### WorkStealingDeque (include/work_stealing_deque.h)
- Per-worker intrusive doubly-linked deque
- **Concurrency contract:**
  - Owner: push_back / pop_back (LIFO end)
  - Thieves: steal_front (FIFO end)
  - All operations acquire the deque mutex (fine-grained, one lock per deque)
- Invariants:
  - A task is in exactly one deque at a time (no duplicates)
  - front→...→back via next pointers; back→...→front via prev pointers
  - Empty: front == back == nullptr, size == 0

## Thread Pool Implementations

### GlobalQueuePool (Phase 0)
- N worker threads all contend on a single GlobalQueue
- Workers wait on a shared condition variable
- Baseline for measuring work-stealing improvement

### WorkStealingPool (Phase 1)
- N worker threads, each with a private WorkStealingDeque
- External submissions round-robin across worker deques
- Idle workers steal from a random victim's deque front
- Overflow GlobalQueue as final fallback
- Workers use short timed waits (100μs) before rechecking

## Synchronization Model

| Operation | Lock | Contention |
|-----------|------|------------|
| Owner push/pop | Per-deque mutex | Low (only conflicts with steal) |
| Steal | Per-deque mutex (victim's) | Low (random victim selection) |
| GlobalQueue push/pop | Single global mutex | Higher (all threads) |
| Pending counter | atomic int64 | Minimal (fetch_add/sub) |
| Shutdown flag | atomic bool | Read-only hot path |

## Concurrency Invariants

1. A task exists in exactly one location: a deque, the overflow queue, or executing
2. `pending_` count == number of tasks enqueued but not yet completed
3. `wait()` blocks until `pending_` reaches 0
4. Shutdown drains all remaining tasks before worker threads exit
5. No task is ever executed twice or dropped

## Memory Ordering

- `pending_` uses `relaxed` for increment (no ordering needed at enqueue)
- `pending_` uses `acq_rel` for decrement (ensures task work is visible before signaling done)
- `stop_` uses `relaxed` reads in hot path (protected by mutex on the write side)
- `next_worker_` uses `relaxed` (round-robin doesn't need strict ordering)

## Phase 0 → Phase 1 Improvement

Phase 0 has all workers contending on a single mutex for the global queue.
Phase 1 distributes work across N deques, so in the common case workers only
touch their own deque. Contention drops from O(N) to O(1) for local operations,
with occasional steal operations adding brief cross-thread contention.

## Known Limitations (to address in Phase 2+)

- Deque uses mutex for all operations (could use lock-free CAS for owner ops)
- No batch stealing (steals one task at a time)
- No backoff strategy for idle workers (fixed 100μs wait)
- No task priorities
- No CPU affinity / NUMA awareness