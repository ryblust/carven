#pragma once

#include <carven/runtime/execution/clock.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace carven::runtime::io::detail {

enum class ReadMode { Chunk, Line, All };

struct Request final {
    ReadMode mode;
    std::size_t limit;
    std::size_t scanned;
};

// The view remains valid until the next fill or consume on its process cursor.
// A scheduler constructs its owning completion before committing consumption.
struct ByteView final {
    std::span<const std::uint8_t> bytes;
    bool end;
};

struct ReadError final {
    std::int32_t code;
    bool limited;
};

using ReadyResult = std::variant<ByteView, ReadError>;

// One cursor per runtime instance retains incomplete records, EOF, and system
// failures.
// Readiness borrows bytes; cancellation belongs to the consuming scheduler.
class InputCursor final {
    std::vector<std::uint8_t> pending;
    bool ended = false;
    std::int32_t error = 0;
#if defined(_WIN32)
    wchar_t high_surrogate = 0;
    bool console_lines = false;
    std::size_t console_committed = 0;
#endif

    auto view(std::size_t count, bool end) const noexcept -> ReadyResult {
        return ByteView {.bytes = std::span(pending).first(count), .end = end};
    }

public:
    InputCursor() = default;
    InputCursor(const InputCursor&) = delete;
    auto operator=(const InputCursor&) -> InputCursor& = delete;

    static auto process() noexcept -> InputCursor& {
        static auto cursor = InputCursor();
        return cursor;
    }

    auto consume(std::size_t count) noexcept -> void {
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
#if defined(_WIN32)
        console_committed -= std::min(console_committed, count);
#endif
    }

    auto ready(Request& request) noexcept -> std::optional<ReadyResult> {
        if (request.mode == ReadMode::Chunk && request.limit == 0) {
            return view(0, false);
        }
        if (error != 0) {
            return ReadError {.code = error, .limited = false};
        }
        if (request.mode == ReadMode::Chunk) {
#if defined(_WIN32)
            const auto available = console_lines && !ended ? console_committed : pending.size();
#else
            const auto available = pending.size();
#endif
            if (available != 0) {
                return view(std::min(request.limit, available), false);
            }
            if (ended) {
                return view(0, true);
            }
            return std::nullopt;
        }
        if (request.mode == ReadMode::Line) {
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
                return ReadError {.code = 0, .limited = true};
            }
            if (newline != pending.end()) {
                return view(content + 1, false);
            }
            request.scanned = pending.size();
        } else if (pending.size() > request.limit) {
            return ReadError {.code = 0, .limited = true};
        }
        if (ended) {
            return view(pending.size(), pending.empty());
        }
        return std::nullopt;
    }

    auto fill() noexcept -> void {
        if (ended || error != 0) {
            return;
        }
        std::array<std::uint8_t, 4096> bytes;
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
            const auto character = record.Event.KeyEvent.uChar.UnicodeChar;
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

    auto wait(std::optional<std::int64_t> deadline) noexcept -> void {
#if defined(_WIN32)
        const auto remaining = runtime::execution::detail::steady_poll_timeout(deadline);
        const auto milliseconds = remaining < 0 ? 10 : std::min(10, remaining);
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#else
        const auto milliseconds = runtime::execution::detail::steady_poll_timeout(deadline);
        auto descriptor = pollfd {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
        if (::poll(&descriptor, 1, milliseconds) < 0 && errno != EINTR) {
            error = errno;
        }
#endif
    }
};

#if defined(_WIN32)

// Inherited stdout and CreatePipe handles are synchronous. Keep their blocking
// write off the execution driver, retaining the borrowed bytes until joined.
class WriteWorker final {
    HANDLE output = nullptr;
    HANDLE worker = nullptr;
    std::span<const std::uint8_t> bytes;
    std::atomic<bool> stopping = false;
    std::atomic<bool> completed = false;
    std::int32_t error = 0;

    static auto WINAPI run(void* context) noexcept -> DWORD;

public:
    WriteWorker() = default;
    WriteWorker(const WriteWorker&) = delete;
    auto operator=(const WriteWorker&) -> WriteWorker& = delete;
    ~WriteWorker() noexcept;

    auto start(HANDLE handle, std::span<const std::uint8_t> data) noexcept -> std::int32_t;
    auto started() const noexcept -> bool;
    auto ready() const noexcept -> bool;
    auto result() const noexcept -> std::int32_t;
    auto cancel() noexcept -> void;
    auto close() noexcept -> void;
    auto wait(DWORD milliseconds) const noexcept -> void;
};

inline auto WINAPI WriteWorker::run(void* context) noexcept -> DWORD {
    auto& write = *static_cast<WriteWorker*>(context);
    auto offset = std::size_t();
    while (offset < write.bytes.size() && !write.stopping.load(std::memory_order_acquire)) {
        const auto size =
            static_cast<DWORD>(std::min(write.bytes.size() - offset, std::size_t(65536)));
        auto count = DWORD();
        if (!WriteFile(write.output, write.bytes.data() + offset, size, &count, nullptr)) {
            write.error = static_cast<std::int32_t>(GetLastError());
            break;
        }
        if (count == 0) {
            write.error = ERROR_WRITE_FAULT;
            break;
        }
        offset += count;
    }
    write.completed.store(true, std::memory_order_release);
    return 0;
}

inline WriteWorker::~WriteWorker() noexcept {
    close();
}

inline auto WriteWorker::close() noexcept -> void {
    if (worker) {
        // Cancellation can race the worker's entry into WriteFile. Repeat until
        // the thread exits; CancelSynchronousIo itself does not wait for completion.
        while (WaitForSingleObject(worker, 0) == WAIT_TIMEOUT) {
            cancel();
            WaitForSingleObject(worker, 1);
        }
        CloseHandle(worker);
        worker = nullptr;
    }
}

inline auto WriteWorker::start(HANDLE handle, std::span<const std::uint8_t> data) noexcept
    -> std::int32_t {
    output = handle;
    bytes = data;
    worker = CreateThread(nullptr, 0, run, this, 0, nullptr);
    return worker ? 0 : static_cast<std::int32_t>(GetLastError());
}

inline auto WriteWorker::started() const noexcept -> bool {
    return worker != nullptr;
}

inline auto WriteWorker::ready() const noexcept -> bool {
    return completed.load(std::memory_order_acquire);
}

inline auto WriteWorker::result() const noexcept -> std::int32_t {
    return error;
}

inline auto WriteWorker::cancel() noexcept -> void {
    stopping.store(true, std::memory_order_release);
    if (worker) {
        CancelSynchronousIo(worker);
    }
}

inline auto WriteWorker::wait(DWORD milliseconds) const noexcept -> void {
    WaitForSingleObject(
        worker,
        stopping.load(std::memory_order_acquire) ? std::min(milliseconds, DWORD(1)) : milliseconds
    );
}

#endif

} // namespace carven::runtime::io::detail
