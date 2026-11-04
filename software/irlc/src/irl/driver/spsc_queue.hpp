#pragma once
// An implementation of a simple lock-free queue for a single producer and consumer

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <new>
#include <thread>

// This is single producer single consumer which greatly simplifies things. In addition to that we
// assert that:
// 1. There is always a node in the list
// 2. Only producer writes to & reads from mTail - thus mTail will lag by at most one node
// 3. Only consumer writes to & reads from mHead - the node pointed to my mHead is a dummy not in
// the list Popping takes ownership, pushing relinquishes ownership
template <class T> class SpscQueue {
  public:
    struct alignas(std::hardware_destructive_interference_size) QNode {
        std::atomic<QNode *> next;
        // Must be non-null for all but the dummy node
        std::unique_ptr<T> data;
    };

  private:
    // Wake up consumer waiting on pop.
    // Will be incremented with release after a push occurs, thus should be waited on with acquire
    // Needs to be a shared_ptr bc other threads than this queue might want to wake us up,
    // but what if this queue gets freed?
    std::shared_ptr<std::atomic<size_t>> mPopWaitersWakeup;
    // Head ptr. Does not need to be atomic bc only the poper accesses it
    // Still align bc false sharing
    QNode *mHead;
    // Tail ptr. Does not need to be atomic bc only the poper accesses it
    // Still align bc false sharing
    alignas(std::hardware_destructive_interference_size) QNode *mTail;

  public:
    SpscQueue() : mPopWaitersWakeup(new std::atomic<size_t>(0)) {
        QNode *nullNode = new QNode(nullptr, nullptr);
        mHead = nullNode;
        mHead = nullNode;
    }

    // Tries to pop. Null if nothing to pop
    std::unique_ptr<T> try_pop() {
        QNode *next = mHead->next.load(std::memory_order_acquire);
        std::unique_ptr<T> out{nullptr};
        if (next) {
            // Take data
            out = std::move(next->data);
            // advance head
            mHead = next;
            // free head
            delete mHead;
        }
        return out;
    }

    // Tries to pop and waits until mPopWaitersWakeup is modified
    // Null if someone woke us up but no data
    std::unique_ptr<T> pop_or_wake() {
        size_t oldwakeup = mPopWaitersWakeup->load(std::memory_order_acquire);
        if (auto out = try_pop()) {
            return out;
        } else {
            // We loaded mPopWaitersWakeup and head->next was a nullptr (bc try_pop failed)
            // They could push right now, but since they increment mPopWaitersWakeup after
            // mHead->next mPopWaitersWakeup will 100% be different than the one we loaded earlier
            mPopWaitersWakeup->wait(oldwakeup, std::memory_order_acquire);
            // Try pop one more time in case we got woken up by a push.
            // Again mHead->next would have been updated so this will succeed
            return try_pop();
        }
    }

    // Tries to pop for timeout in ms
    std::unique_ptr<T> try_pop_for_timeout(std::chrono::milliseconds ms) {
        size_t oldwakeup = mPopWaitersWakeup->load(std::memory_order_acquire);
        if (auto out = try_pop()) {
            return out;
        } else {
            // Spawn a thread that waits, checks if we got woken, and if so does nothing other
            // otherwise wakes us up
            std::thread(
                [ms, oldwakeup](std::weak_ptr<std::atomic<size_t>> popwaitersptr) {
                    std::this_thread::sleep_for(ms);
                    auto wakeup = popwaitersptr.lock();
                    if (wakeup && wakeup->load(std::memory_order_relaxed) != oldwakeup) {
                        wakeup->fetch_add(1, std::memory_order_relaxed);
                    }
                },
                mPopWaitersWakeup)
                .detach();

            mPopWaitersWakeup->wait(oldwakeup, std::memory_order_acquire);
            return try_pop();
        }
    }

    std::weak_ptr<std::atomic<size_t>> get_waker_token() { return mPopWaitersWakeup; }

    // Removes everything from queue
    // Only safe for consumer to call
    void drain() {
        while (try_pop()) {
        }
    }

    void push(std::unique_ptr<T> data) {
        // Make a new node
        QNode *newnode = new QNode{
            .next = nullptr,
            .data = std::move(data),
        };
        // Add it to list. Even if everything popped this will remain in list due to always one
        // node.
        mTail->next.store(newnode, std::memory_order_release);
        // Advance tail. node def valid
        mTail = newnode;
        wakeup_waiters();
    }

    void wakeup_waiters() {
        mPopWaitersWakeup->fetch_add(1, std::memory_order_release);
        mPopWaitersWakeup->notify_all();
    }
};
