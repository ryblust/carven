#pragma once

#include <carven/runtime/collection/slice.hpp>
#include <carven/runtime/execution/execution.hpp>

#include <cstdint>
#include <cstdlib>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace input_test {

inline auto verify(bool condition) noexcept -> void {
    if (!condition) {
        std::abort();
    }
}

class Pipe final {
#if defined(_WIN32)
    HANDLE saved = INVALID_HANDLE_VALUE;
    HANDLE reader = INVALID_HANDLE_VALUE;
    HANDLE writer = INVALID_HANDLE_VALUE;
#else
    int saved = -1;
    int writer = -1;
#endif

public:
    auto open() noexcept -> void {
#if defined(_WIN32)
        verify(CreatePipe(&reader, &writer, nullptr, 0));
        saved = GetStdHandle(STD_INPUT_HANDLE);
        verify(SetStdHandle(STD_INPUT_HANDLE, reader));
#else
        int descriptors[2];
        verify(::pipe(descriptors) == 0);
        saved = ::dup(STDIN_FILENO);
        verify(saved >= 0 && ::dup2(descriptors[0], STDIN_FILENO) >= 0);
        ::close(descriptors[0]);
        writer = descriptors[1];
#endif
    }

    auto write(std::string_view text) noexcept -> void {
#if defined(_WIN32)
        DWORD count = 0;
        verify(WriteFile(writer, text.data(), static_cast<DWORD>(text.size()), &count, nullptr));
        verify(count == text.size());
#else
        verify(::write(writer, text.data(), text.size()) == static_cast<ssize_t>(text.size()));
#endif
    }

    auto close() noexcept -> void {
#if defined(_WIN32)
        CloseHandle(writer);
        writer = INVALID_HANDLE_VALUE;
#else
        ::close(writer);
        writer = -1;
#endif
    }

    auto consumed() const noexcept -> bool {
#if defined(_WIN32)
        auto remaining = DWORD {};
        verify(PeekNamedPipe(reader, nullptr, 0, nullptr, &remaining, nullptr));
#else
        auto remaining = int {};
        verify(::ioctl(STDIN_FILENO, FIONREAD, &remaining) == 0);
#endif
        return remaining == 0;
    }

    ~Pipe() noexcept {
#if defined(_WIN32)
        if (saved != INVALID_HANDLE_VALUE) {
            SetStdHandle(STD_INPUT_HANDLE, saved);
        }
        if (reader != INVALID_HANDLE_VALUE) {
            CloseHandle(reader);
        }
        if (writer != INVALID_HANDLE_VALUE) {
            CloseHandle(writer);
        }
#else
        if (saved >= 0) {
            ::dup2(saved, STDIN_FILENO);
            ::close(saved);
        }
        if (writer >= 0) {
            ::close(writer);
        }
#endif
    }
};

inline auto process_pipe() noexcept -> Pipe& {
    static auto pipe = Pipe();
    return pipe;
}

} // namespace input_test

inline auto io_test_open_pipe() noexcept -> void {
    input_test::process_pipe().open();
}

inline auto io_test_invalidate_input() noexcept -> void {
#if defined(_WIN32)
    input_test::verify(SetStdHandle(STD_INPUT_HANDLE, INVALID_HANDLE_VALUE));
#else
    input_test::verify(::close(STDIN_FILENO) == 0);
#endif
}

inline auto io_test_write(carven::runtime::Slice<std::uint8_t> bytes) noexcept -> void {
    input_test::process_pipe().write(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
    );
}

inline auto io_test_close_pipe() noexcept -> void {
    input_test::process_pipe().close();
}

inline auto io_test_wait_consumed() noexcept -> carven::runtime::execution::AsyncOperation<void> {
    using namespace carven::runtime::execution;
    while (!input_test::process_pipe().consumed()) {
        co_await yield_once();
    }
    co_return Completion<void>::success();
}
