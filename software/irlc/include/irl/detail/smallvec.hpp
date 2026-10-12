#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <irl/detail/util.hpp>

namespace irl {
namespace detail {

// A vector with inline storage for up to AMT elements. Once it outgrows the
// inline buffer it moves to the heap.
//
// The sign of mSize encodes where the elements live: a non-negative value means
// the inline array is active, while a negative value means the heap buffer is
// active and the size is encoded as -(size + 1). The +1 is important: it keeps
// "heap, size 0" distinct from "inline, size 0" so the allocation is not lost
// when the vector is emptied.
template <typename T, size_t AMT> class SmallVec {
    static_assert(AMT > 0, "SmallVec needs room for at least one inline element");

    // >= 0 = inline (value is the size)
    // <  0 = heap  (size is -(mSize + 1))
    int64_t mSize;
    union {
        T mSmallBacking[AMT];
        struct {
            T *arr;
            size_t capacity;
        } mLargeBacking;
    };

  private:
    inline bool is_local() const { return mSize >= 0; }

    inline void set_size_no_relocate(int64_t newsize) {
        assert(newsize >= 0);
        mSize = is_local() ? newsize : -(newsize + 1);
    }

    static T *allocate(size_t n) { return std::allocator<T>().allocate(n); }
    static void deallocate(T *p, size_t n) { std::allocator<T>().deallocate(p, n); }

    // Move all live elements into a freshly allocated heap buffer of the given
    // capacity. Leaves the vector on the heap with the same size.
    void grow_to_heap(int64_t newcapacity) {
        const int64_t n = size();
        T *old = begin();
        T *newarr = allocate(static_cast<size_t>(newcapacity));

        for (int64_t i = 0; i < n; i++) {
            new (newarr + i) T(std::move(old[i]));
        }
        // Destroy the moved-from elements.
        for (int64_t i = 0; i < n; i++) {
            old[i].~T();
        }
        if (!is_local()) {
            deallocate(old, static_cast<size_t>(mLargeBacking.capacity));
        }

        mLargeBacking.arr = newarr;
        mLargeBacking.capacity = static_cast<size_t>(newcapacity);
        mSize = -(n + 1); // mark as heap, preserving size
    }

  public:
    SmallVec() : mSize(0) {}

    SmallVec(SmallVec &&other) : mSize(0) {
        if (&other == this) {
            return;
        }

        if (other.is_local()) {
            const int64_t n = other.size();
            for (int64_t i = 0; i < n; i++) {
                new (mSmallBacking + i) T(std::move(other.mSmallBacking[i]));
            }
            for (int64_t i = 0; i < n; i++) {
                other.mSmallBacking[i].~T();
            }
            mSize = n;
        } else {
            mLargeBacking = other.mLargeBacking;
            mSize = other.mSize;
        }
        // Leave `other` empty and inline so it does not free the buffer we took.
        other.mSize = 0;
    }

    SmallVec(const SmallVec &) = delete;
    SmallVec &operator=(const SmallVec &) = delete;
    SmallVec &operator=(SmallVec &&) = delete;

    ~SmallVec() {
        for (T &elem : *this) {
            elem.~T();
        }
        if (!is_local()) {
            deallocate(mLargeBacking.arr, mLargeBacking.capacity);
        }
    }

    // Grow to at least newcap, doubling the current capacity when that is
    // larger. This must satisfy requests larger than 2x the current capacity.
    void ensure_capacity(int64_t newcap) {
        if (newcap <= capacity()) {
            return;
        }
        int64_t doubled = capacity() * 2;
        int64_t target = newcap > doubled ? newcap : doubled;
        grow_to_heap(target);
    }

    void resize(int64_t newsize) {
        ASSERT(newsize >= 0, "new size < 0");

        const int64_t current = size();
        if (newsize <= current) {
            T *b = begin();
            for (int64_t i = newsize; i < size(); i++) {
                b[i].~T();
            }
            set_size_no_relocate(newsize);
        } else {
            ensure_capacity(newsize);

            T *b = begin();
            for (int64_t i = size(); i < newsize; i++) {
                new (b + i) T();
            }
            set_size_no_relocate(newsize);
        }
    }

    void clear() { resize(0); }

    void push_back(T &&other) {
        ensure_capacity(size() + 1);

        new (begin() + size()) T(std::move(other));

        set_size_no_relocate(size() + 1);
    }

    void push_back(T const &other) {
        ensure_capacity(size() + 1);

        new (begin() + size()) T(other);

        set_size_no_relocate(size() + 1);
    }

    T const &operator[](size_t i) const {
        ASSERT(i < static_cast<size_t>(size()), "Index out of bounds");
        return *(begin() + i);
    }
    T &operator[](size_t i) {
        ASSERT(i < static_cast<size_t>(size()), "Index out of bounds");
        return *(begin() + i);
    }

    int64_t capacity() const {
        return is_local() ? static_cast<int64_t>(AMT)
                          : static_cast<int64_t>(mLargeBacking.capacity);
    }

    int64_t size() const { return is_local() ? mSize : -(mSize + 1); }

    T const *begin() const { return is_local() ? mSmallBacking : mLargeBacking.arr; }
    T *begin() { return is_local() ? mSmallBacking : mLargeBacking.arr; }
    T const *end() const { return begin() + size(); }
    T *end() { return begin() + size(); }
};

}; // namespace detail
}; // namespace irl
