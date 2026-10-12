#pragma once

#include <cstdlib>
#include <iostream>
#include <source_location>

namespace irl {
namespace detail {

inline void ASSERT(bool condition, const char *message,
                   std::source_location loc = std::source_location::current()) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << " - " << loc.file_name() << ":"
                  << loc.line() << " (" << loc.function_name() << ")" << std::endl;
        std::abort();
    }
}

}; // namespace detail
}; // namespace irl
