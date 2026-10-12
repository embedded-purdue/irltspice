#pragma once

#include <cstdint>
#include <ostream>
#include <string_view>

#include "util/macros.hpp"

// The value of a component in pico-somethings
typedef struct val_pico {
    uint64_t v;

    // Throws if unknown suffix
    constexpr val_pico() = default;
    constexpr val_pico(uint64_t v) : v(v) {};
    //                                                        10^12 = 123456789ABC
    constexpr static inline val_pico no_scaling(uint64_t v) { return val_pico(v * 1000000000000); };
    static val_pico from_str(std::string_view str);

    // Holy const
    // Also I can now see why resut has Derive
    constexpr bool operator==(val_pico const &other) const { return v == other.v; };
    constexpr bool operator!=(val_pico const &other) const { return v != other.v; };
    constexpr bool operator<(val_pico const &other) const { return v < other.v; };
    constexpr bool operator>(val_pico const &other) const { return v > other.v; };
    constexpr bool operator<=(val_pico const &other) const { return v <= other.v; };
    constexpr bool operator>=(val_pico const &other) const { return v >= other.v; };

    constexpr val_pico operator+(val_pico const &other) const { return v + other.v; };
    constexpr val_pico operator-(val_pico const &other) const { return v - other.v; };
    constexpr val_pico operator*(val_pico const &other) const { return v * other.v; };
    constexpr val_pico operator/(val_pico const &other) const { return v / other.v; };
    constexpr val_pico operator%(val_pico const &other) const { return v % other.v; };

} val_pico_t;

std::ostream &operator<<(std::ostream &os, val_pico_t const &val);

constexpr static inline val_pico_t operator""_p(operator_t n) { return val_pico_t(n); }

constexpr static inline val_pico_t operator""_n(operator_t n) { return operator""_p(n * 1000); }

constexpr static inline val_pico_t operator""_u(operator_t n) { return operator""_n(n * 1000); }

constexpr static inline val_pico_t operator""_m(operator_t n) { return operator""_u(n * 1000); }

constexpr static inline val_pico_t operator""_k(operator_t n) {
    return val_pico_t::no_scaling(n * 1000);
}

constexpr static inline val_pico_t operator""_M(operator_t n) { return operator""_k(n * 1000); }
