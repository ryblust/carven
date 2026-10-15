module;

#include <cerrno>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif


module carven:support.input.impl;

import :support.clock;
import :support.input;
import std;

namespace {

auto host_poll_timeout(std::optional<std::int64_t> deadline) noexcept -> int {
    if (!deadline) {
        return -1;
    }
    const auto instant = host_monotonic_now();
    if (*deadline <= instant) {
        return 0;
    }
    const auto remaining =
        static_cast<std::uint64_t>(*deadline) - static_cast<std::uint64_t>(instant);
    const auto milliseconds = remaining / 1000000 + (remaining % 1000000 != 0);
    return milliseconds > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(milliseconds);
}

} // namespace

auto HostInputCursor::view(std::size_t count, bool end) const noexcept -> HostInputResult {
    return HostInputPacket {.bytes = std::span(pending).first(count), .end = end};
}

auto HostInputCursor::process() noexcept -> HostInputCursor& {
    static auto cursor = HostInputCursor();
    return cursor;
}

auto HostInputCursor::consume(std::size_t count) noexcept -> void {
    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
#if defined(_WIN32)
    console_committed -= std::min(console_committed, count);
#endif
}

auto HostInputCursor::ready(HostInputRequest& request) noexcept -> std::optional<HostInputResult> {
    if (request.mode == HostInputMode::Chunk && request.limit == 0) {
        return view(0);
    }
    if (error != 0) {
        return HostInputSystemError {.code = error};
    }
    if (request.mode == HostInputMode::Chunk) {
        auto available = pending.size();
#if defined(_WIN32)
        if (console_lines && !ended) {
            available = console_committed;
        }
#endif
        if (available != 0) {
            return view(std::min(request.limit, available));
        }
        if (ended) {
            return view(0, true);
        }
        return std::nullopt;
    }
    if (request.mode == HostInputMode::Line) {
        request.scanned = std::min(request.scanned, pending.size());
        const auto begin = pending.begin() + static_cast<std::ptrdiff_t>(request.scanned);
        const auto newline = std::find(begin, pending.end(), std::uint8_t('\n'));
        const auto content = static_cast<std::size_t>(newline - pending.begin());
        // A trailing CR may still become the first half of CRLF. At EOF
        // without LF it is ordinary content and counts toward the limit.
        const auto length =
            content != 0 && pending[content - 1] == '\r' && (newline != pending.end() || !ended)
            ? content - 1
            : content;
        if (length > request.limit) {
            return HostInputLimitError {};
        }
        if (newline != pending.end()) {
            return view(content + 1);
        }
        request.scanned = pending.size();
    } else if (pending.size() > request.limit) {
        return HostInputLimitError {};
    }
    if (ended) {
        return view(pending.size(), pending.empty());
    }
    return std::nullopt;
}

auto HostInputCursor::fill() noexcept -> void {
    if (ended || error != 0) {
        return;
    }
    auto bytes = std::array<std::uint8_t, 4096>();
#if defined(_WIN32)
    const auto handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        error = ERROR_INVALID_HANDLE;
        return;
    }
    const auto kind = GetFileType(handle);
    auto console_mode = DWORD {};
    if (kind == FILE_TYPE_CHAR && GetConsoleMode(handle, &console_mode)) {
        console_lines = (console_mode & ENABLE_LINE_INPUT) != 0;
        const auto readiness = WaitForSingleObject(handle, 0);
        if (readiness == WAIT_FAILED) {
            error = static_cast<std::int32_t>(GetLastError());
            return;
        }
        if (readiness != WAIT_OBJECT_0) {
            return;
        }
        auto record = INPUT_RECORD {};
        auto count = DWORD {};
        if (!ReadConsoleInputW(handle, &record, 1, &count)) {
            error = static_cast<std::int32_t>(GetLastError());
            return;
        }
        if (count == 0 || record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            return;
        }
        auto character = record.Event.KeyEvent.uChar.UnicodeChar;
        if (character == 0) {
            return;
        }
        if (character == 26) {
            if (high_surrogate != 0) {
                error = ERROR_NO_UNICODE_TRANSLATION;
            } else {
                ended = true;
            }
            return;
        }
        if (character >= 0xd800 && character <= 0xdbff) {
            if (high_surrogate != 0) {
                error = ERROR_NO_UNICODE_TRANSLATION;
                return;
            }
            high_surrogate = character;
            return;
        }
        auto characters = std::array<wchar_t, 2> {character, 0};
        auto length = 1;
        if (high_surrogate != 0) {
            characters = {std::exchange(high_surrogate, 0), character};
            length = 2;
        } else if (character == '\r') {
            characters[0] = '\n';
        } else if (character == '\b' && console_lines) {
            if (!pending.empty() && pending.back() != '\n') {
                auto start = pending.size() - 1;
                while (start != 0 && (pending[start] & 0xc0) == 0x80) {
                    --start;
                }
                pending.resize(start);
                if ((console_mode & ENABLE_ECHO_INPUT) != 0) {
                    auto output_count = DWORD {};
                    WriteConsoleW(
                        GetStdHandle(STD_OUTPUT_HANDLE),
                        L"\b \b",
                        3,
                        &output_count,
                        nullptr
                    );
                }
            }
            return;
        }
        const auto written = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            characters.data(),
            length,
            reinterpret_cast<char*>(bytes.data()),
            static_cast<int>(bytes.size()),
            nullptr,
            nullptr
        );
        if (written == 0) {
            error = static_cast<std::int32_t>(GetLastError());
            return;
        }
        for (auto repeat = WORD {}; repeat < record.Event.KeyEvent.wRepeatCount; ++repeat) {
            pending.insert(pending.end(), bytes.begin(), bytes.begin() + written);
        }
        if (characters[0] == '\n') {
            console_committed = pending.size();
        }
        if ((console_mode & ENABLE_ECHO_INPUT) != 0) {
            auto output_count = DWORD {};
            if (characters[0] == '\n') {
                characters = {L'\r', L'\n'};
                length = 2;
            }
            WriteConsoleW(
                GetStdHandle(STD_OUTPUT_HANDLE),
                characters.data(),
                static_cast<DWORD>(length),
                &output_count,
                nullptr
            );
        }
        return;
    }
    auto available = DWORD {};
    if (kind == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
            const auto failure = GetLastError();
            if (failure == ERROR_BROKEN_PIPE) {
                ended = true;
            } else {
                error = static_cast<std::int32_t>(failure);
            }
            return;
        }
        if (available == 0) {
            return;
        }
    } else {
        available = static_cast<DWORD>(bytes.size());
    }
    auto count = DWORD {};
    if (!ReadFile(
            handle,
            bytes.data(),
            std::min(available, static_cast<DWORD>(bytes.size())),
            &count,
            nullptr
        )) {
        const auto failure = GetLastError();
        if (failure == ERROR_BROKEN_PIPE) {
            ended = true;
        } else {
            error = static_cast<std::int32_t>(failure);
        }
        return;
    }
    if (count == 0) {
        ended = true;
    } else {
        pending.insert(pending.end(), bytes.begin(), bytes.begin() + count);
    }
#else
    auto descriptor = pollfd {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    const auto status = ::poll(&descriptor, 1, 0);
    if (status < 0) {
        if (errno != EINTR) {
            error = errno;
        }
        return;
    }
    if (status == 0) {
        return;
    }
    if ((descriptor.revents & POLLNVAL) != 0) {
        error = EBADF;
        return;
    }
    const auto count = ::read(STDIN_FILENO, bytes.data(), bytes.size());
    if (count < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            error = errno;
        }
    } else if (count == 0) {
        ended = true;
    } else {
        pending.insert(pending.end(), bytes.begin(), bytes.begin() + count);
    }
#endif
}

auto HostInputCursor::wait(std::optional<std::int64_t> deadline) noexcept -> void {
#if defined(_WIN32)
    const auto remaining = host_poll_timeout(deadline);
    const auto milliseconds = remaining < 0 ? 10 : std::min(10, remaining);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#else
    const auto milliseconds = host_poll_timeout(deadline);
    auto descriptor = pollfd {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    if (::poll(&descriptor, 1, milliseconds) < 0 && errno != EINTR) {
        error = errno;
    }
#endif
}
