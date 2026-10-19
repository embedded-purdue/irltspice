#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace irl {
namespace detail {

// Customization point for ChunkedList.
//
// A Chunkable<T> specialization teaches ChunkedList how to thread its free list
// through T's own storage and how to mark a slot as dead, so no extra storage is
// needed for either. T is never default-constructed: a fresh chunk is made of
// raw bytes, so every operation below MUST be valid on *uninitialized* storage
// (mark_dead/set_freelist are the only things that touch a new slot) and on a
// slot whose T has already been destroyed (get_freelist/is_dead, when the slot
// is on the free list).
//
// Members are private on purpose: ChunkedList is the only client. A
// specialization declares it as a friend and defines the four hooks below.
template <typename T> class Chunkable;

template <typename T, std::size_t ChunkSize, typename Trait = Chunkable<T>> class ChunkedList;

template <typename T> class Chunkable {
    template <typename, std::size_t, typename> friend class ChunkedList;
};

// A pointer-stable, append-only pool of T laid out as a singly-linked list of
// fixed-size chunks. allocate() hands back raw storage (the caller placement-news
// the T); deallocate() destroys the T and returns the slot to the free list.
// Slots never move once handed out, so pointers stay valid until deallocate().
template <typename T, std::size_t ChunkSize, typename Trait> class ChunkedList {
    static_assert(ChunkSize > 0, "ChunkedList needs at least one slot per chunk");

    struct Chunk {
        Chunk *mNext;
        alignas(T) std::byte mSlots[sizeof(T) * ChunkSize];

        T &operator[](std::size_t i) { return *(reinterpret_cast<T *>(mSlots) + i); }
        const T &operator[](std::size_t i) const {
            return *(reinterpret_cast<const T *>(mSlots) + i);
        }
    };

    Chunk *mChunks = nullptr;
    Chunk *mChunkTail = nullptr;
    T *mFreeHead = nullptr;

    // The iterator cannot reach Trait's private hooks directly (friendship is
    // not inherited by nested classes), so expose the one predicate it needs.
    static bool is_dead(const T &v) { return Trait::is_dead(v); }

    void make_chunk() {
        Chunk *c =
            static_cast<Chunk *>(::operator new(sizeof(Chunk), std::align_val_t{alignof(Chunk)}));
        c->mNext = nullptr;
        if (mChunkTail == nullptr) {
            assert(mChunks == nullptr);
            mChunks = c;
        } else {
            mChunkTail->mNext = c;
        }
        mChunkTail = c;

        // Thread every slot onto the free list. Descending index order so that
        // allocation hands out slots in ascending order. No T is constructed.
        for (std::size_t i = ChunkSize; i-- > 0;) {
            T &slot = (*c)[i];
            Trait::mark_dead(slot);
            Trait::set_freelist(slot, mFreeHead);
            mFreeHead = &slot;
        }
    }

  public:
    // Forward iterator over live (non-tombstone) elements.
    template <bool Const> class IterImpl {
        friend class ChunkedList;
        template <bool> friend class IterImpl;

      public:
        using value_type = std::conditional_t<Const, const T, T>;
        using reference = value_type &;
        using pointer = value_type *;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

      private:
        using ChunkPtr = std::conditional_t<Const, const Chunk *, Chunk *>;

        ChunkPtr mChunk = nullptr;
        std::size_t mIdx = 0;

        IterImpl(ChunkPtr chunk, std::size_t idx) : mChunk(chunk), mIdx(idx) { skip_dead(); }

        void skip_dead() {
            while (mChunk != nullptr) {
                if (mIdx >= ChunkSize) {
                    mChunk = mChunk->mNext;
                    mIdx = 0;
                    continue;
                }
                if (!ChunkedList::is_dead((*mChunk)[mIdx])) {
                    return;
                }
                ++mIdx;
            }
        }

      public:
        IterImpl() = default;

        // Non-const -> const conversion.
        template <bool Other>
            requires(Const && !Other)
        IterImpl(const IterImpl<Other> &other) : mChunk(other.mChunk), mIdx(other.mIdx) {}

        reference operator*() const { return (*mChunk)[mIdx]; }
        pointer operator->() const { return std::addressof((*mChunk)[mIdx]); }

        IterImpl &operator++() {
            ++mIdx;
            skip_dead();
            return *this;
        }
        IterImpl operator++(int) {
            IterImpl copy = *this;
            ++*this;
            return copy;
        }

        bool operator==(const IterImpl &other) const {
            return mChunk == other.mChunk && mIdx == other.mIdx;
        }
        bool operator!=(const IterImpl &other) const { return !(*this == other); }
    };

    using Iter = IterImpl<false>;
    using ConstIter = IterImpl<true>;

    ChunkedList() noexcept = default;
    ChunkedList(const ChunkedList &) = delete;
    ChunkedList &operator=(const ChunkedList &) = delete;
    ChunkedList(ChunkedList &&other) {
        this->mChunks = other.mChunks;
        this->mChunkTail = other.mChunkTail;
        this->mFreeHead = other.mFreeHead;
        other.mChunks = nullptr;
        other.mChunkTail = nullptr;
        other.mFreeHead = nullptr;
    }
    ChunkedList &operator=(ChunkedList &&other) {
        std::swap(this->mChunks, other.mChunks);
        std::swap(this->mChunkTail, other.mChunkTail);
        std::swap(this->mFreeHead, other.mFreeHead);
    }

    ~ChunkedList() {
        for (Chunk *c = mChunks; c != nullptr; c = c->mNext) {
            for (std::size_t i = 0; i < ChunkSize; ++i) {
                T &slot = (*c)[i];
                if (!is_dead(slot)) {
                    slot.~T();
                }
            }
        }

        Chunk *c = mChunks;
        while (c != nullptr) {
            Chunk *next = c->mNext;
            ::operator delete(c, std::align_val_t{alignof(Chunk)});
            c = next;
        }
    }

    // Raw storage for one slot. The caller must begin a T's lifetime with
    // placement new before using it.
    T *allocate() {
        if (mFreeHead == nullptr) {
            make_chunk();
        }
        T *slot = mFreeHead;
        mFreeHead = Trait::get_freelist(*slot);
        return slot;
    }

    // Destroys the T at `slot` and returns it to the free list. `slot` must have
    // come from this list's allocate() and must not be freed twice.
    void deallocate(T *slot) {
        slot->~T();
        Trait::mark_dead(*slot);
        Trait::set_freelist(*slot, mFreeHead);
        mFreeHead = slot;
    }

    Iter begin() { return Iter(mChunks, 0); }
    Iter end() { return Iter(nullptr, 0); }
    ConstIter begin() const { return ConstIter(mChunks, 0); }
    ConstIter end() const { return ConstIter(nullptr, 0); }
};

} // namespace detail
} // namespace irl
