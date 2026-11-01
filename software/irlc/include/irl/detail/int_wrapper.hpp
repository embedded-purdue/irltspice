#pragma once

#include <compare>
#include <type_traits>

#include "irl/detail/util.hpp"

// An int wrapper to be used with CRTP.
//
// T must derive from IntWrapper<T, I>, be constructible from a single I, and be
// (statically) convertible to I, i.e. expose its underlying integer value. I
// must be an integral type. Every operation is constexpr.
//
// The binary operators are hidden friends rather than members. Mixed operands
// (`t + 1`) are the tricky part: if T were implicitly convertible to I both the
// built-in operators and a member taking I would be viable and neither would be
// a strictly better match, which is ambiguous. Two things avoid that: each
// operator is provided in (T, T), (T, I) and (I, T) flavours, so one of them
// wins outright, and the wrappers are expected to make their conversion to I
// explicit (see NetlistVertexIdx). The result is that arithmetic on a handle
// yields a handle, not a bare I.
//
// Additionally, if the handle has an is_null method it asserts everything is
// non-null
template <typename T, typename I> struct IntWrapper {
    static_assert(std::is_integral_v<I>, "IntWrapper requires an integral underlying type");

    constexpr T &self() noexcept { return *static_cast<T *>(this); }
    constexpr T const &self() const noexcept { return *static_cast<T const *>(this); }

    // T is only ever used here, after the enclosing class is instantiated, but
    // the conversion itself is part of the required contract.
    constexpr I underlying() const noexcept {
        if constexpr (requires { self().is_null(); }) {
            IRL_ASSERT(!self().is_null(), "Attempted to convert null to underlying int.");
        }
        return static_cast<I>(self());
    }

    // Unary
    constexpr T operator+() const { return T{static_cast<I>(+underlying())}; }
    constexpr T operator-() const { return T{static_cast<I>(-underlying())}; }
    constexpr T operator~() const { return T{static_cast<I>(~underlying())}; }
    constexpr bool operator!() const { return !underlying(); }

    // Like an int, the wrapper is truthy when non-zero. Explicit so it does not
    // feed the built-in arithmetic operators.
    explicit constexpr operator bool() const { return underlying() != I{0}; }

    // Increment / decrement
    constexpr T &operator++() { return *this += I{1}; }
    constexpr T operator++(int) {
        T old = self();
        ++*this;
        return old;
    }
    constexpr T &operator--() { return *this -= I{1}; }
    constexpr T operator--(int) {
        T old = self();
        --*this;
        return old;
    }

    // Compound assignment
    constexpr T &operator+=(I other) {
        self() = T{static_cast<I>(underlying() + other)};
        return self();
    }
    constexpr T &operator-=(I other) {
        self() = T{static_cast<I>(underlying() - other)};
        return self();
    }
    constexpr T &operator*=(I other) {
        self() = T{static_cast<I>(underlying() * other)};
        return self();
    }
    constexpr T &operator/=(I other) {
        self() = T{static_cast<I>(underlying() / other)};
        return self();
    }
    constexpr T &operator%=(I other) {
        self() = T{static_cast<I>(underlying() % other)};
        return self();
    }
    constexpr T &operator&=(I other) {
        self() = T{static_cast<I>(underlying() & other)};
        return self();
    }
    constexpr T &operator|=(I other) {
        self() = T{static_cast<I>(underlying() | other)};
        return self();
    }
    constexpr T &operator^=(I other) {
        self() = T{static_cast<I>(underlying() ^ other)};
        return self();
    }
    constexpr T &operator<<=(I other) {
        self() = T{static_cast<I>(underlying() << other)};
        return self();
    }
    constexpr T &operator>>=(I other) {
        self() = T{static_cast<I>(underlying() >> other)};
        return self();
    }

// Logical operators. Overloading these loses short-circuiting, but an index
// handle has no side effects, so the observable behaviour matches an int.
#define IRL_INTWRAP_LOGICAL_OP(OP)                                                                 \
    friend constexpr bool operator OP(T const &a, T const &b) {                                    \
        return a.underlying() OP b.underlying();                                                   \
    }                                                                                              \
    friend constexpr bool operator OP(T const &a, I b) { return a.underlying() OP b; }             \
    friend constexpr bool operator OP(I a, T const &b) { return a OP b.underlying(); }

    IRL_INTWRAP_LOGICAL_OP(&&)
    IRL_INTWRAP_LOGICAL_OP(||)

#undef IRL_INTWRAP_LOGICAL_OP

// Binary arithmetic and bitwise operators in all three operand-type mixes.
#define IRL_INTWRAP_BINARY_OP(OP)                                                                  \
    friend constexpr T operator OP(T const &a, T const &b) {                                       \
        return T{static_cast<I>(a.underlying() OP b.underlying())};                                \
    }                                                                                              \
    friend constexpr T operator OP(T const &a, I b) {                                              \
        return T{static_cast<I>(a.underlying() OP b)};                                             \
    }                                                                                              \
    friend constexpr T operator OP(I a, T const &b) {                                              \
        return T{static_cast<I>(a OP b.underlying())};                                             \
    }

    IRL_INTWRAP_BINARY_OP(+)
    IRL_INTWRAP_BINARY_OP(-)
    IRL_INTWRAP_BINARY_OP(*)
    IRL_INTWRAP_BINARY_OP(/)
    IRL_INTWRAP_BINARY_OP(%)
    IRL_INTWRAP_BINARY_OP(&)
    IRL_INTWRAP_BINARY_OP(|)
    IRL_INTWRAP_BINARY_OP(^)
    IRL_INTWRAP_BINARY_OP(<<)
    IRL_INTWRAP_BINARY_OP(>>)

#undef IRL_INTWRAP_BINARY_OP

    // Comparisons. `<=>` plus `==` lets the compiler synthesize `<`, `<=`, `>`,
    // `>=` and `!=`.
    friend constexpr bool operator==(T const &a, T const &b) {
        return a.underlying() == b.underlying();
    }
    friend constexpr bool operator==(T const &a, I b) { return a.underlying() == b; }
    friend constexpr bool operator==(I a, T const &b) { return a == b.underlying(); }

    friend constexpr std::strong_ordering operator<=>(T const &a, T const &b) {
        return a.underlying() <=> b.underlying();
    }
    friend constexpr std::strong_ordering operator<=>(T const &a, I b) {
        return a.underlying() <=> b;
    }
    friend constexpr std::strong_ordering operator<=>(I a, T const &b) {
        return a <=> b.underlying();
    }
};
