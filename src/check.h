#pragma once

#include <cstdio>
#include <cstdlib>
#include <format>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <system_error>

class CRError : public std::runtime_error {
public:
    template <class... Args>
    explicit CRError(std::format_string<Args...> fmt, Args&&... args) :
        std::runtime_error(std::format(fmt, std::forward<Args>(args)...)) {}
};

class SystemError : public std::system_error {
public:
    template <class... Args>
    SystemError(int errnum, std::format_string<Args...> fmt, Args&&... args) :
        std::system_error(errnum, std::system_category(), std::format(fmt, std::forward<Args>(args)...)) {}
};

[[noreturn, gnu::cold, gnu::noinline]]
inline void assertion_failed(const char* cond, std::source_location loc, std::string_view msg = {}) {
    std::fprintf(
        stderr,
        "coroutine-runtime: %s:%u: assertion failed: %s%s%.*s\n",
        loc.file_name(),
        loc.line(),
        cond,
        msg.empty() ? "" : ": ",
        static_cast<int>(msg.size()),
        msg.data()
    );
    std::abort();
}

template <class... Args>
[[noreturn, gnu::cold, gnu::noinline]]
void assertion_failed(const char* cond, std::source_location loc, std::format_string<Args...> fmt, Args&&... args) {
    assertion_failed(cond, loc, std::format(fmt, std::forward<Args>(args)...));
}

#define CR_ASSERT(cond, ...)                                                                                           \
    do {                                                                                                               \
        if (!(cond)) [[unlikely]]                                                                                      \
            assertion_failed(#cond, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);                       \
    } while (0)

#ifdef NDEBUG
#define CR_DEBUG_ASSERT(cond, ...) ((void)0)
#else
#define CR_DEBUG_ASSERT(cond, ...) CR_ASSERT(cond __VA_OPT__(, ) __VA_ARGS__)
#endif
