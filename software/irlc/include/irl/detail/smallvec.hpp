#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <irl/detail/util.hpp>

namespace irl {
namespace detail {

template <typename T, size_t AMT> class SmallVec {
    // <0 = on heap
    // >=0 = in array (local)
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

    void grow_to_heap(int64_t newcapacity) {
        T *newarr = new T[newcapacity];

        // Holy cursed
        for (T *newi = newarr, *oldi = begin(); oldi != end(); oldi++, newi++) {
            new (newi) T(std::move(*oldi));
        }

        if (!is_local()) {
            delete[] mLargeBacking.arr;
        }

        mLargeBacking.arr = newarr;
        mLargeBacking.capacity = newcapacity;
        // Don't change size, just indicate we on heap
        mSize = -size();
    }

    void put_sz_no_change_loc(int64_t newsize) {
        if (is_local()) {
            mSize = newsize;
        } else {
            mSize = -newsize;
        }
    }

    void shrink_to(int64_t newsize) {
        for (auto i = begin() + newsize; i != end(); i++) {
            i->~T();
        }
        put_sz_no_change_loc(newsize);
    }

  public:
    SmallVec() : mSize(0), mSmallBacking{0} {
        static_assert(sizeof(mSmallBacking) >= sizeof(mLargeBacking), "Don't waste space");
    };

    SmallVec(SmallVec &&other) {
        if (&other == this) {
            return;
        }
        if (!is_local()) {
            delete[] mLargeBacking.arr;
        }

        mSize = other.mSize;
        if (other.is_local()) {
            mSmallBacking = other.mSmallBacking;
        } else {
            mLargeBacking = other.mLargeBacking;
        }
        // Ensures it will think it's local so it won't access ptr
        other.mSize = 0;
        assert(other.is_local());
    }

    ~SmallVec() {
        if (!is_local()) {
            delete[] mLargeBacking.arr;
        }
    }

    void ensure_capacity(int64_t newcap) {
        if (newcap > capacity()) {
            grow_to_heap(capacity() * 2);
        }
    }

    void resize(int64_t newsize) {
        ASSERT(newsize > 0, "new size < 0");

        if (newsize <= size()) {
            shrink_to(newsize);
        } else {
            ensure_capacity(newsize);

            T *b = begin();
            for (int64_t size_current = size(); size_current < newsize; size_current++) {
                new (b + size_current) T();
            }

            put_sz_no_change_loc(newsize);
        }
    }

    void clear() { resize(0); }

    void push_back(T &&other) {
        ensure_capacity(size() + 1);

        new (begin() + size()) T(std::move(other));

        put_sz_no_change_loc(size() + 1);
    }

    T const &operator[](size_t i) const {
        ASSERT(i < size(), "Index out of bounds");
        return *(begin() + i);
    }
    T &operator[](size_t i) {
        ASSERT(i < size(), "Index out of bounds");
        return *(begin() + i);
    }

    int64_t capacity() const { return is_local() ? AMT : mLargeBacking.capacity; }

    int64_t size() const { return mSize < 0 ? -mSize : mSize; }

    T const *begin() const { return is_local() ? &(mSmallBacking[0]) : mLargeBacking.arr; }
    T *begin() { return is_local() ? &(mSmallBacking[0]) : mLargeBacking.arr; }
    T const *end() const { return begin() + size(); }
    T *end() { return begin() + size(); }
};

}; // namespace detail
}; // namespace irl
