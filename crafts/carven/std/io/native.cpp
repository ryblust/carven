#include <carven/std/io/native.hpp>

#include <carven/runtime/io/stream.hpp>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <optional>
#include <utility>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace carven::standard::io {

auto write_stdout(std::string_view bytes) noexcept -> std::int32_t {
#if defined(_WIN32)
    if (::_setmode(::_fileno(stdout), _O_BINARY) < 0) {
        return errno;
    }
#endif
    if (std::fwrite(bytes.data(), 1, bytes.size(), stdout) != bytes.size()) {
        return errno == 0 ? EIO : errno;
    }
    if (std::fflush(stdout) != 0) {
        return errno == 0 ? EIO : errno;
    }
    return 0;
}

namespace {

namespace async = runtime::execution;
std::atomic_flag output_busy = ATOMIC_FLAG_INIT;

class Output final : public async::EventSource {
    using Completion = async::Completion<std::int32_t>;
    runtime::Vector<std::uint8_t> bytes;
#if defined(_WIN32)
    runtime::io::detail::WriteWorker writer;
#else
    std::size_t offset = 0;
    int flags = -1;
#endif
    bool active = false;
    bool attached = false;
    async::ResumeContinuation continuation;
    std::optional<Completion> result;

    auto finish(int code) noexcept -> bool;
    auto attempt() noexcept -> bool;
    auto suspend() noexcept -> bool;

public:
    explicit Output(runtime::Vector<std::uint8_t> bytes) noexcept;
    ~Output() noexcept override;

    auto await_ready() const noexcept -> bool;

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
        continuation.bind(handle);
        return suspend();
    }

    auto await_resume() noexcept -> Completion;
    auto poll(async::Driver& driver) noexcept -> void override;
    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override;
};

Output::Output(runtime::Vector<std::uint8_t> bytes) noexcept
    : bytes(std::move(bytes)) {}

Output::~Output() noexcept {
    if (attached) {
        continuation.driver().detach(*this);
    }
#if defined(_WIN32)
    writer.close();
#else
    if (flags >= 0) {
        ::fcntl(STDOUT_FILENO, F_SETFL, flags);
    }
#endif
    if (active) {
        output_busy.clear();
    }
}

auto Output::finish(int code) noexcept -> bool {
    result.emplace(Completion::success_from([&]() noexcept {
        return static_cast<std::int32_t>(code);
    }));
    return true;
}

auto Output::attempt() noexcept -> bool {
    if (continuation.cancel_requested()) {
#if defined(_WIN32)
        if (writer.started()) {
            writer.cancel();
            if (!writer.ready()) {
                return false;
            }
        }
#endif
        result.emplace(Completion::cancelled());
        return true;
    }
#if defined(_WIN32)
    if (bytes.empty()) {
        return finish(0);
    }
    if (!writer.started()) {
        const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!handle || handle == INVALID_HANDLE_VALUE) {
            return finish(ERROR_INVALID_HANDLE);
        }
        const auto code = writer.start(handle, std::span(bytes.begin(), bytes.size()));
        if (code != 0) {
            return finish(code);
        }
    }
    return writer.ready() ? finish(writer.result()) : false;
#else
    while (offset < bytes.size()) {
        // Block SIGPIPE only on this thread and consume only a newly generated
        // signal. A closed editor pipe must be a recoverable EPIPE failure.
        sigset_t blocked, previous, pending;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        const auto mask_error = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous);
        if (mask_error != 0) {
            return finish(mask_error);
        }
        ::sigpending(&pending);
        const bool had_signal = sigismember(&pending, SIGPIPE) == 1;
        const auto count = ::write(STDOUT_FILENO, bytes.begin() + offset, bytes.size() - offset);
        const auto code = errno;
        if (count < 0 && code == EPIPE && !had_signal) {
            ::sigpending(&pending);
            if (sigismember(&pending, SIGPIPE) == 1) {
                int signal;
                ::sigwait(&blocked, &signal);
            }
        }
        ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && code == EINTR) {
            continue;
        }
        if (count < 0 && (code == EAGAIN || code == EWOULDBLOCK)) {
            return false;
        }
        return finish(count == 0 ? EIO : code);
    }
#endif
    return finish(0);
}

auto Output::suspend() noexcept -> bool {
    if (continuation.cancel_requested()) {
        result.emplace(Completion::cancelled());
        return false;
    }
    if (output_busy.test_and_set()) {
        finish(EBUSY);
        return false;
    }
    active = true;
#if !defined(_WIN32)
    flags = ::fcntl(STDOUT_FILENO, F_GETFL);
    if (flags < 0 || ::fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        finish(errno);
        return false;
    }
#endif
    if (attempt()) {
        return false;
    }
    continuation.driver().attach(*this);
    attached = true;
    return true;
}

auto Output::await_ready() const noexcept -> bool {
    return false;
}

auto Output::await_resume() noexcept -> Completion {
    return std::move(*result);
}

auto Output::poll(async::Driver& driver) noexcept -> void {
    if (!result && attempt()) {
        continuation.enqueue(driver);
    }
}

auto Output::wait(std::optional<std::int64_t> deadline) noexcept -> bool {
#if !defined(_WIN32)
    if (result) {
        return false;
    }
    auto descriptor = pollfd {.fd = STDOUT_FILENO, .events = POLLOUT, .revents = 0};
    if (::poll(&descriptor, 1, async::detail::steady_poll_timeout(deadline)) < 0
        && errno != EINTR) {
        finish(errno);
        continuation.enqueue(continuation.driver());
    }
    return true;
#else
    if (result) {
        return false;
    }
    const auto timeout = async::detail::steady_poll_timeout(deadline);
    writer.wait(timeout < 0 ? INFINITE : static_cast<DWORD>(timeout));
    return true;
#endif
}

} // namespace

auto write_output(runtime::Vector<std::uint8_t> bytes) noexcept
    -> runtime::execution::AsyncOperation<std::int32_t> {
    co_return co_await Output(std::move(bytes));
}

} // namespace carven::standard::io
