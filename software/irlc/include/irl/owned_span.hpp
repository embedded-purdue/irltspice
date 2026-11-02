#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <span>

#include <irl/detail/util.hpp>

namespace irl {

// Basically a unique_ptr to a contiguous range of T: it owns a heap buffer of
// `size()` elements, is move-only, and indexes with bounds checking. It is
// implicitly convertible to std::span for read-only or mutable use.
template <typename T> class OwnedSpan {
    T *mData = nullptr;
    int64_t mSize = 0;

    static T *allocate(int64_t n) { return std::allocator<T>().allocate(n); }
    static void deallocate(T *p, int64_t n) { std::allocator<T>().deallocate(p, n); }

    void destroy() {
        if (mData == nullptr) {
            return;
        }
        for (int64_t i = 0; i < mSize; i++) {
            mData[i].~T();
        }
        deallocate(mData, mSize);
        mData = nullptr;
        mSize = 0;
    }

  public:
    using type = T;
    OwnedSpan() = default;

    // Copy-construct each element of [begin, end) into a fresh buffer.
    template <typename It> OwnedSpan(It begin, It end) {
        mSize = static_cast<int64_t>(std::distance(begin, end));
        if (mSize == 0) {
            return;
        }
        mData = allocate(mSize);
        int64_t i = 0;
        for (It it = begin; it != end; ++it, ++i) {
            new (mData + i) T(*it);
        }
    }

    OwnedSpan &operator=(const OwnedSpan &other) {
        destroy();
        new (this) OwnedSpan(other.begin(), other.end());
    };
    OwnedSpan(const OwnedSpan &other) { new (this) OwnedSpan(other.begin(), other.end()); };

    OwnedSpan(OwnedSpan &&other) noexcept : mData(other.mData), mSize(other.mSize) {
        other.mData = nullptr;
        other.mSize = 0;
    }

    OwnedSpan &operator=(OwnedSpan &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        destroy();
        mData = other.mData;
        mSize = other.mSize;
        other.mData = nullptr;
        other.mSize = 0;
        return *this;
    }

    ~OwnedSpan() { destroy(); }

    T const &operator[](int64_t i) const {
        IRL_ASSERT(i < mSize, "Index out of bounds");
        return mData[i];
    }
    T &operator[](int64_t i) {
        IRL_ASSERT(i < mSize, "Index out of bounds");
        return mData[i];
    }

    T const *data() const { return mData; }
    T *data() { return mData; }
    int64_t size() const { return mSize; }
    bool empty() const { return mSize == 0; }

    T const *begin() const { return mData; }
    T *begin() { return mData; }
    T const *end() const { return mData + mSize; }
    T *end() { return mData + mSize; }

    operator std::span<T>() { return std::span<T>(mData, mSize); }
    operator std::span<const T>() const { return std::span<const T>(mData, mSize); }
};

}; // namespace irl
