#pragma once

#include <ranges>

template <typename T> static inline std::ranges::subrange<T> pair_to_iter(std::pair<T, T> pair) {
    return std::ranges::subrange(pair.first, pair.second);
}
