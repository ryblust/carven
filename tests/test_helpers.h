#pragma once

#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include "doctest.h"

#include <cstdio>
#if defined(_WIN32)
    #include <io.h>
#else
    #include <unistd.h>
#endif

namespace carven_test {

#if defined(_WIN32)
inline auto file_number(FILE* file) noexcept -> int { return _fileno(file); }
inline auto duplicate_fd(int fd) noexcept -> int { return _dup(fd); }
inline auto duplicate_fd_to(int from, int to) noexcept -> int { return _dup2(from, to); }
inline auto close_fd(int fd) noexcept -> int { return _close(fd); }
inline constexpr auto NULL_DEVICE = "NUL";
#else
inline auto file_number(FILE* file) noexcept -> int { return fileno(file); }
inline auto duplicate_fd(int fd) noexcept -> int { return dup(fd); }
inline auto duplicate_fd_to(int from, int to) noexcept -> int { return dup2(from, to); }
inline auto close_fd(int fd) noexcept -> int { return close(fd); }
inline constexpr auto NULL_DEVICE = "/dev/null";
#endif

class ScopedOutputSilence final {
public:
    ScopedOutputSilence() noexcept {
        std::fflush(stdout);
        std::fflush(stderr);

        null_file = std::fopen(NULL_DEVICE, "w");
        if (null_file == nullptr) return;

        const auto null_fd = file_number(null_file);
        saved_stdout = duplicate_fd(file_number(stdout));
        saved_stderr = duplicate_fd(file_number(stderr));
        if (saved_stdout < 0 || saved_stderr < 0) return;

        (void)duplicate_fd_to(null_fd, file_number(stdout));
        (void)duplicate_fd_to(null_fd, file_number(stderr));
    }

    ScopedOutputSilence(const ScopedOutputSilence&) = delete;
    auto operator=(const ScopedOutputSilence&) -> ScopedOutputSilence& = delete;

    ~ScopedOutputSilence() noexcept {
        std::fflush(stdout);
        std::fflush(stderr);

        if (saved_stdout >= 0) {
            duplicate_fd_to(saved_stdout, file_number(stdout));
            close_fd(saved_stdout);
        }
        if (saved_stderr >= 0) {
            duplicate_fd_to(saved_stderr, file_number(stderr));
            close_fd(saved_stderr);
        }
        if (null_file != nullptr) {
            std::fclose(null_file);
        }
    }

private:
    FILE* null_file = nullptr;
    int saved_stdout = -1;
    int saved_stderr = -1;
};

template<typename Func>
auto run_silenced(Func func) noexcept(noexcept(func())) -> decltype(func()) {
    const auto silence = ScopedOutputSilence();
    return func();
}

}
