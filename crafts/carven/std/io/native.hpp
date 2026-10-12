#pragma once

#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/execution/execution.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace carven::io {

struct ReadResult final {
    runtime::Vector<std::uint8_t> bytes;
    std::int32_t error = 0;
    bool end = false;
    bool limited = false;

    auto take_bytes() noexcept -> runtime::Vector<std::uint8_t> { return std::move(bytes); }
};

// One process input cursor owns prefetch and incomplete records. Cold operations
// do not touch it. Cancellation unlinks only the request, retaining unread bytes.
class InputQueue final : public runtime::execution::EventSource {
    using Completion = runtime::execution::Completion<ReadResult>;

public:
    class Awaiter final {
        friend class InputQueue;
        InputQueue& queue;
        std::uint8_t mode;
        std::size_t limit;
        std::size_t scanned = 0;
        Awaiter* next = nullptr;
        std::optional<Completion> completion;
        runtime::execution::ResumeContinuation resume;

    public:
        Awaiter(InputQueue& owner, std::uint8_t kind, std::size_t maximum) noexcept
            : queue(owner),
              mode(kind),
              limit(maximum) {}

        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) = delete;

        ~Awaiter() noexcept {
            runtime::execution::detail::require_protocol(!resume.bound() || completion.has_value());
        }

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
            resume.bind(handle);
            if (resume.cancel_requested()) {
                completion.emplace(Completion::cancelled());
                return false;
            }
            queue.insert(*this);
            return true;
        }

        auto await_resume() noexcept -> Completion { return std::move(*completion); }
    };

private:
    Awaiter* first = nullptr;
    Awaiter* last = nullptr;
    std::vector<std::uint8_t> pending;
    bool ended = false;
    std::int32_t error = 0;
#if defined(_WIN32)
    wchar_t high_surrogate = 0;
    bool console_lines = false;
    std::size_t console_committed = 0;
#endif

    auto insert(Awaiter& request) noexcept -> void {
        auto& driver = request.resume.driver();
        if (first == nullptr) {
            driver.attach(*this);
        }
        if (last != nullptr) {
            last->next = &request;
        } else {
            first = &request;
        }
        last = &request;
    }

    auto finish(
        Awaiter& request,
        Awaiter* previous,
        Completion result,
        runtime::execution::Driver& driver
    ) noexcept -> void {
        if (previous != nullptr) {
            previous->next = request.next;
        } else {
            first = request.next;
        }
        if (last == &request) {
            last = previous;
        }
        request.next = nullptr;
        request.completion.emplace(std::move(result));
        if (first == nullptr) {
            driver.detach(*this);
        }
        request.resume.enqueue(driver);
    }

    auto take(std::size_t count, bool end = false) noexcept -> ReadResult {
        auto result =
            ReadResult {.bytes = runtime::Vector<std::uint8_t>::with_capacity(count), .end = end};
        for (std::size_t index = 0; index < count; ++index) {
            result.bytes.push(pending[index]);
        }
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
#if defined(_WIN32)
        console_committed -= std::min(console_committed, count);
#endif
        return result;
    }

    auto ready(Awaiter& request) noexcept -> std::optional<ReadResult> {
        if (error != 0) {
            return ReadResult {.bytes = {}, .error = error};
        }
        if (request.mode == 0) {
            if (request.limit == 0) {
                return ReadResult {};
            }
            auto available = pending.size();
#if defined(_WIN32)
            if (console_lines && !ended) {
                available = console_committed;
            }
#endif
            if (available != 0) {
                return take(std::min(request.limit, available));
            }
            if (ended) {
                return ReadResult {.bytes = {}, .end = true};
            }
            return std::nullopt;
        }
        if (request.mode == 1) {
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
                return ReadResult {.bytes = {}, .limited = true};
            }
            if (newline != pending.end()) {
                return take(content + 1);
            }
            request.scanned = pending.size();
        } else if (pending.size() > request.limit) {
            return ReadResult {.bytes = {}, .limited = true};
        }
        if (ended) {
            return take(pending.size(), pending.empty());
        }
        return std::nullopt;
    }

    auto fill() noexcept -> void {
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

    auto poll(runtime::execution::Driver& driver) noexcept -> void override {
        auto* previous = static_cast<Awaiter*>(nullptr);
        auto* request = first;
        while (request != nullptr) {
            auto* next = request->next;
            if (request->resume.cancel_requested()) {
                finish(*request, previous, Completion::cancelled(), driver);
            } else {
                previous = request;
            }
            request = next;
        }
        if (first == nullptr) {
            return;
        }
        auto result = ready(*first);
        if (!result) {
            fill();
            result = ready(*first);
        }
        if (result) {
            finish(
                *first,
                nullptr,
                Completion::success_from([&]() noexcept -> ReadResult {
                    return std::move(*result);
                }),
                driver
            );
        }
    }

    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override {
        if (first == nullptr) {
            return false;
        }
#if defined(_WIN32)
        auto milliseconds = std::int64_t(10);
        if (deadline) {
            milliseconds = std::min(
                milliseconds,
                std::max<std::int64_t>(
                    0,
                    (*deadline - runtime::execution::detail::steady_now() + 999999) / 1000000
                )
            );
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#else
        auto milliseconds = -1;
        if (deadline) {
            const auto remaining = std::max<std::int64_t>(
                0,
                (*deadline - runtime::execution::detail::steady_now() + 999999) / 1000000
            );
            milliseconds = static_cast<int>(
                std::min<std::int64_t>(remaining, std::numeric_limits<int>::max())
            );
        }
        auto descriptor = pollfd {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
        if (::poll(&descriptor, 1, milliseconds) < 0 && errno != EINTR) {
            error = errno;
        }
#endif
        return true;
    }

public:
    static auto process() noexcept -> InputQueue& {
        static auto queue = InputQueue();
        return queue;
    }

    auto read(std::uint8_t mode, std::size_t limit) noexcept -> Awaiter {
        return Awaiter(*this, mode, limit);
    }
};

} // namespace carven::io

inline auto carven_io_unlimited() noexcept -> std::size_t {
    return std::numeric_limits<std::size_t>::max();
}

inline auto carven_io_read(std::uint8_t mode, std::size_t limit) noexcept
    -> carven::runtime::execution::AsyncOperation<carven::io::ReadResult> {
    co_return co_await carven::io::InputQueue::process().read(mode, limit);
}
