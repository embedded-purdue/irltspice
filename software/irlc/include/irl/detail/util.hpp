#pragma once

#include <cstdlib>
#include <format>
#include <iostream>
#include <source_location>

namespace irl {
namespace detail {

template <typename... Args> struct LogContext {
    std::format_string<Args...> fmt;
    std::source_location location;

    // Needed for default args for the source_location
    LogContext(std::format_string<Args...> fmt,
               std::source_location loc = std::source_location::current())
        : fmt(fmt), location(loc) {}
};

template <typename... Args>
inline void ASSERT(bool condition, const char *cond_str, LogContext<Args...> ctx, Args &&...args) {
    auto loc = ctx.location;
    if (!condition) {
        std::cerr << "Assertion \"" << cond_str
                  << "\" failed: " << std::format(ctx.fmt, std::forward<Args>(args)...) << " - "
                  << loc.file_name() << ":" << loc.line() << " (" << loc.function_name() << ")"
                  << std::endl;
        std::abort();
    }
}

#define IRL_ASSERT(COND, FMT_STR, ...)                                                             \
    ::irl::detail::ASSERT(COND, #COND, {FMT_STR} __VA_OPT__(, ) __VA_ARGS__)

}; // namespace detail
}; // namespace irl
