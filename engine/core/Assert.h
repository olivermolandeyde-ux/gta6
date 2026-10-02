#pragma once

#include "core/Types.h"

#include <source_location>
#include <cstdio>
#include <cstdlib>

namespace engine {

[[noreturn]] inline void panic_impl(const char* expression,
                                    const char* message,
                                    const std::source_location location) {
    std::fprintf(stderr,
                 "[LEONIDA][FATAL] %s:%u in %s\n  expr: %s\n  msg : %s\n",
                 location.file_name(),
                 location.line(),
                 location.function_name(),
                 expression ? expression : "(none)",
                 message ? message : "(none)");
    std::abort();
}

inline void assert_impl(bool condition,
                        const char* expression,
                        const char* message,
                        const std::source_location location = std::source_location::current()) {
    if (!condition) {
        panic_impl(expression, message, location);
    }
}

} // namespace engine

#define ENGINE_ASSERT(cond, msg) \
    ::engine::assert_impl(static_cast<bool>(cond), #cond, (msg))

#define ENGINE_PANIC(msg) \
    ::engine::panic_impl("(panic)", (msg), std::source_location::current())

#define ENGINE_UNREACHABLE() ENGINE_PANIC("unreachable code path")
