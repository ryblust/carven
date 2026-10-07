#include "pipe.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <span>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
namespace async = carven::runtime::async;
using async_interop::PipeError;
using async_interop::PipeQueue;
using Result = async::Completion<std::size_t, PipeError>;
using Operation = async::Operation<std::size_t, PipeError>;

auto check(bool condition, const char* message) noexcept -> void {
    if (!condition) {
        std::fprintf(stderr, "pipe contract failed: %s (errno=%d)\n", message, errno);
        std::exit(EXIT_FAILURE);
    }
}

auto balanced(const PipeQueue& queue) noexcept -> void {
    check(queue.pending_count() == 0, "no pending pipe registration");
    check(
        queue.registration_count() == queue.completion_count(),
        "registration/unregistration balance"
    );
}

class Pipe final {
    int descriptors[2];

public:
    Pipe() noexcept {
        check(::pipe(descriptors) == 0, "create real pipe");
        for (const auto fd : descriptors) {
            const auto flags = ::fcntl(fd, F_GETFL);
            check(
                flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0,
                "set nonblocking pipe"
            );
        }
    }

    Pipe(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    auto operator=(const Pipe&) -> Pipe& = delete;
    auto operator=(Pipe&&) -> Pipe& = delete;

    ~Pipe() noexcept {
        for (const auto fd : descriptors) {
            if (fd >= 0) {
                check(::close(fd) == 0, "close pipe descriptor");
            }
        }
    }

    auto reader() const noexcept -> int { return descriptors[0]; }

    auto writer() const noexcept -> int { return descriptors[1]; }

    auto close_writer() noexcept -> void {
        check(::close(descriptors[1]) == 0, "close writer to establish EOF");
        descriptors[1] = -1;
    }
};

auto raw_write(int fd, std::span<const std::byte> data) noexcept -> std::size_t {
    ssize_t count;
    do {
        count = ::write(fd, data.data(), data.size());
    } while (count < 0 && errno == EINTR);
    check(count >= 0, "write fixture bytes");
    return static_cast<std::size_t>(count);
}

auto raw_read(int fd, std::span<std::byte> data) noexcept -> std::size_t {
    ssize_t count;
    do {
        count = ::read(fd, data.data(), data.size());
    } while (count < 0 && errno == EINTR);
    check(count >= 0, "read fixture bytes");
    return static_cast<std::size_t>(count);
}

auto bytes(Result& result) noexcept -> std::size_t {
    check(result.success_if() != nullptr, "pipe operation succeeds");
    return result.success_if()->value;
}

struct Observation final {
    int started;
    int finished;
    bool cancelled;
};

auto read(PipeQueue& queue, int fd, std::span<std::byte> buffer, Observation& observed) noexcept
    -> Operation {
    ++observed.started;
    auto result = co_await queue.read_some(fd, buffer);
    observed.cancelled = result.is_cancelled();
    ++observed.finished;
    co_return result;
}

auto write(
    PipeQueue& queue,
    int fd,
    std::span<const std::byte> buffer,
    Observation& observed
) noexcept -> Operation {
    ++observed.started;
    auto result = co_await queue.write_some(fd, buffer);
    observed.cancelled = result.is_cancelled();
    ++observed.finished;
    co_return result;
}

auto cold() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    const auto data = std::array {std::byte {42}};
    auto buffer = std::array<std::byte, 1> {};
    check(raw_write(pipe.writer(), data) == 1, "preload cold read observation");
    {
        auto reading = read(queue, pipe.reader(), buffer, observed);
        auto writing = write(queue, pipe.writer(), data, observed);
        check(
            observed.started == 0 && queue.registration_count() == 0,
            "cold construction does not execute I/O"
        );
    }
    check(
        raw_read(pipe.reader(), buffer) == 1 && buffer == data,
        "cold read leaves queued bytes unchanged"
    );
    check(
        ::read(pipe.reader(), buffer.data(), buffer.size()) == -1
            && (errno == EAGAIN || errno == EWOULDBLOCK),
        "cold write leaves empty pipe unchanged"
    );
    check(
        observed.started == 0 && observed.finished == 0,
        "dropped operations never execute payload"
    );
    balanced(queue);
}

auto transfer_body(PipeQueue& queue, Pipe& pipe) noexcept -> async::Operation<void> {
    const auto data = std::array {std::byte {1}, std::byte {2}, std::byte {3}};
    auto buffer = std::array<std::byte, 4> {};
    auto written = co_await queue.write_some(pipe.writer(), data);
    check(bytes(written) == data.size(), "ready write returns exact byte count");
    auto received = co_await queue.read_some(pipe.reader(), buffer);
    check(
        bytes(received) == data.size()
            && buffer[0] == data[0]
            && buffer[1] == data[1]
            && buffer[2] == data[2],
        "read_some returns available bytes without filling larger requested buffer"
    );
    co_return async::Completion<void>::success();
}

auto transfer() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto result = async::drive_root(transfer_body(queue, pipe), &queue);
    check(
        result.success_if() != nullptr && queue.registration_count() == 0,
        "ready I/O needs no registration"
    );
    balanced(queue);
}

auto eof_body(PipeQueue& queue, Pipe& pipe) noexcept -> async::Operation<void> {
    auto buffer = std::array<std::byte, 2> {};
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(read(queue, pipe.reader(), buffer, observed));
    co_await async::await_yield_once();
    check(queue.pending_count() == 1, "EOF read registers before writer closes");
    const auto data = std::array {std::byte {4}, std::byte {5}, std::byte {6}};
    check(raw_write(pipe.writer(), data) == data.size(), "write bytes before HUP");
    pipe.close_writer();
    auto first = co_await child.observe();
    check(
        bytes(first) == 2 && buffer[0] == std::byte {4} && buffer[1] == std::byte {5},
        "HUP retains first buffered bytes"
    );
    auto second = co_await queue.read_some(pipe.reader(), buffer);
    check(bytes(second) == 1 && buffer[0] == std::byte {6}, "HUP retains remaining buffered byte");
    auto last = co_await queue.read_some(pipe.reader(), buffer);
    check(bytes(last) == 0, "zero read means EOF only after buffered bytes drain");
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

auto eof_error() noexcept -> void {
    auto queue = PipeQueue();
    {
        auto pipe = Pipe();
        auto result = async::drive_root(eof_body(queue, pipe), &queue);
        check(result.success_if() != nullptr, "buffered EOF completes");
    }
    {
        auto pipe = Pipe();
        auto buffer = std::array<std::byte, 1> {};
        auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
        auto result = async::drive_root(read(queue, pipe.writer(), buffer, observed), &queue);
        const auto* error = result.failure_if<PipeError>();
        check(
            error != nullptr && error->code == EBADF,
            "reading valid write-only descriptor reports EBADF"
        );
    }
    check(
        queue.registration_count() == 1,
        "pending HUP read registers once; immediate system error does not register"
    );
    balanced(queue);
}

class PollGate final {
    PipeQueue& queue;
    bool& released;

public:
    PollGate(PipeQueue& provider, bool& allow_poll) noexcept
        : queue(provider),
          released(allow_poll) {}

    auto poll(async::Driver& driver) noexcept -> void {
        if (released) {
            queue.poll(driver);
        }
    }

    auto wait() noexcept -> bool { return queue.wait(); }
};

auto writer_task(PipeQueue& queue, int fd, Observation& reading, bool& released) noexcept
    -> async::Operation<void> {
    check(
        queue.pending_count() == 1 && reading.started == 1 && reading.finished == 0,
        "reader registers EAGAIN before sibling writer starts"
    );
    const auto data = std::array {std::byte {7}, std::byte {8}};
    auto result = co_await queue.write_some(fd, data);
    check(bytes(result) == data.size(), "sibling task writes actual pipe bytes");
    released = true;
    co_return async::Completion<void>::success();
}

auto delayed_body(PipeQueue& queue, Pipe& pipe, bool& released, Observation& observed) noexcept
    -> async::Operation<void> {
    auto buffer = std::array<std::byte, 2> {};
    auto scope = async::ChildScope(co_await async::current_activation());
    auto reader = scope.start(read(queue, pipe.reader(), buffer, observed));
    auto writer = scope.start(writer_task(queue, pipe.writer(), observed, released));
    auto result = co_await reader.observe();
    check(
        bytes(result) == 2 && buffer[0] == std::byte {7} && buffer[1] == std::byte {8},
        "registered read wakes with actual data"
    );
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    check(observed.finished == 1, "delayed reader finishes once before buffer cleanup");
    co_return async::Completion<void>::success();
}

auto delayed() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto released = false;
    auto pump = PollGate(queue, released);
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result = async::drive_root(delayed_body(queue, pipe, released, observed), &pump);
    check(
        result.success_if() != nullptr && queue.registration_count() == 1,
        "EAGAIN read registers exactly once"
    );
    balanced(queue);
}

class ExternalWriter final {
    PipeQueue& queue;
    int fd;
    bool waited = false;

public:
    ExternalWriter(PipeQueue& provider, int descriptor) noexcept
        : queue(provider),
          fd(descriptor) {}

    auto poll(async::Driver& driver) noexcept -> void {
        if (waited || queue.pending_count() == 0) {
            queue.poll(driver);
        }
    }

    auto wait() noexcept -> bool {
        check(
            queue.pending_count() == 1 && !waited,
            "driver reaches idle boundary with registered pipe read"
        );
        waited = true;
        auto writer = std::thread([descriptor = fd]() noexcept {
            const auto data = std::array {std::byte {9}};
            check(
                raw_write(descriptor, data) == 1,
                "external thread writes pipe without touching driver"
            );
        });
        const auto pending = queue.wait();
        writer.join();
        return pending;
    }
};

auto idle_wait() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto pump = ExternalWriter(queue, pipe.writer());
    auto buffer = std::array<std::byte, 1> {};
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result = async::drive_root(read(queue, pipe.reader(), buffer, observed), &pump);
    check(
        bytes(result) == 1 && buffer[0] == std::byte {9} && queue.wait_count() == 1,
        "real poll wait receives external pipe event"
    );
    balanced(queue);
}

class Buffer final {
    PipeQueue& queue;
    bool& alive;
    int& destroyed;

public:
    std::array<std::byte, 1> data {};

    Buffer(PipeQueue& provider, bool& state, int& count) noexcept
        : queue(provider),
          alive(state),
          destroyed(count) {
        alive = true;
    }

    ~Buffer() noexcept {
        check(
            queue.pending_count() == 0,
            "cancel close unlinks all borrowed buffers before cleanup"
        );
        alive = false;
        ++destroyed;
    }
};

auto borrowing_read(
    PipeQueue& queue,
    int fd,
    Buffer& buffer,
    bool& alive,
    Observation& observed
) noexcept -> Operation {
    check(alive, "borrowed buffer lives during registration");
    auto result = co_await read(queue, fd, buffer.data, observed);
    check(
        alive && queue.pending_count() == 0,
        "cancelled registration unlinks before continuation uses live buffer"
    );
    co_return result;
}

auto ancestor(
    PipeQueue& queue,
    int own_fd,
    int child_fd,
    bool& alive,
    int& destroyed,
    Observation& own,
    Observation& descendant
) noexcept -> async::Operation<void> {
    auto buffer = Buffer(queue, alive, destroyed);
    auto own_buffer = std::array<std::byte, 1> {};
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(borrowing_read(queue, child_fd, buffer, alive, descendant));
    auto result = co_await read(queue, own_fd, own_buffer, own);
    check(result.is_cancelled(), "registered ancestor accepts cancellation");
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    check(
        descendant.cancelled && descendant.finished == 1,
        "descendant accepts ancestor cancellation before buffer cleanup"
    );
    co_return async::Completion<void>::cancelled();
}

auto cancelled_close_body(
    PipeQueue& queue,
    Pipe& own_pipe,
    Pipe& child_pipe,
    bool& alive,
    int& destroyed,
    Observation& own,
    Observation& descendant
) noexcept -> async::Operation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(
        ancestor(queue, own_pipe.reader(), child_pipe.reader(), alive, destroyed, own, descendant)
    );
    co_await async::await_yield_once();
    co_await async::await_yield_once();
    check(
        queue.pending_count() == 2 && alive,
        "two real EAGAIN reads register before ancestor cancel"
    );
    async::cancel(child);
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    check(
        !alive && destroyed == 1 && !async::cancellation_requested(),
        "unobserved cancelled child closes once and restores parent context"
    );
    co_return async::Completion<void>::success();
}

auto cancelled_close() noexcept -> void {
    auto own_pipe = Pipe();
    auto child_pipe = Pipe();
    auto queue = PipeQueue();
    auto alive = false;
    auto destroyed = 0;
    auto own = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto descendant = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result = async::drive_root(
        cancelled_close_body(queue, own_pipe, child_pipe, alive, destroyed, own, descendant),
        &queue
    );
    check(
        result.success_if() != nullptr
            && own.cancelled
            && own.finished == 1
            && queue.wait_count() == 0,
        "pending cancel closes without waiting for data"
    );
    balanced(queue);
}

auto committed_body(PipeQueue& queue, Pipe& pipe, bool& released, Observation& observed) noexcept
    -> async::Operation<void> {
    auto& activation = co_await async::current_activation();
    auto buffer = std::array<std::byte, 1> {};
    auto scope = async::ChildScope(activation);
    auto child = scope.start(read(queue, pipe.reader(), buffer, observed));
    co_await async::await_yield_once();
    check(
        queue.pending_count() == 1 && observed.finished == 0,
        "commit test starts at registered read boundary"
    );
    const auto data = std::array {std::byte {10}};
    check(raw_write(pipe.writer(), data) == 1, "make registered descriptor readable");
    queue.poll(*activation.task->driver);
    check(
        queue.pending_count() == 0 && queue.completion_count() == 1 && observed.finished == 0,
        "read syscall commits and unlinks before user continuation dispatch"
    );
    async::cancel(child);
    released = true;
    auto result = co_await child.observe();
    check(
        bytes(result) == 1 && buffer[0] == std::byte {10} && !observed.cancelled,
        "cancel after committed bytes cannot rewrite success"
    );
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

auto committed() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto released = false;
    auto pump = PollGate(queue, released);
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result = async::drive_root(committed_body(queue, pipe, released, observed), &pump);
    check(result.success_if() != nullptr && observed.finished == 1, "committed read finishes once");
    balanced(queue);
}

class Refill final {
    PipeQueue& queue;
    int fd;
    int ordinal = 0;

public:
    Refill(PipeQueue& provider, int descriptor) noexcept
        : queue(provider),
          fd(descriptor) {}

    auto poll(async::Driver& driver) noexcept -> void { queue.poll(driver); }

    auto wait() noexcept -> bool {
        check(
            queue.pending_count() == 1 && ordinal < 2,
            "each rearm reaches a new registered read boundary"
        );
        const auto data = std::array {static_cast<std::byte>(11 + ordinal)};
        ++ordinal;
        check(raw_write(fd, data) == 1, "refill after each read registration");
        return queue.wait();
    }
};

auto rearm_body(PipeQueue& queue, Pipe& pipe) noexcept -> async::Operation<void> {
    auto buffer = std::array<std::byte, 1> {};
    for (const auto value : {std::byte {11}, std::byte {12}}) {
        auto received = co_await queue.read_some(pipe.reader(), buffer);
        check(
            bytes(received) == 1 && buffer[0] == value && queue.pending_count() == 0,
            "destroy completed registered awaiter and reuse descriptor/buffer"
        );
    }
    co_return async::Completion<void>::success();
}

auto rearm() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto pump = Refill(queue, pipe.writer());
    auto result = async::drive_root(rearm_body(queue, pipe), &pump);
    check(
        result.success_if() != nullptr
            && queue.registration_count() == 2
            && queue.wait_count() == 2,
        "rearm registers and completes two reads independently"
    );
    balanced(queue);
}

auto fill(Pipe& pipe) noexcept -> std::size_t {
    const auto block = std::array<std::byte, 4096> {};
    auto filled = std::size_t(0);
    for (const auto width : {block.size(), std::size_t(1)}) {
        for (;;) {
            const auto count = ::write(pipe.writer(), block.data(), width);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0) {
                check(errno == EAGAIN || errno == EWOULDBLOCK, "fill ends at actual pipe capacity");
                break;
            }
            check(count > 0, "fill write makes progress");
            filled += static_cast<std::size_t>(count);
        }
    }
    check(filled > 0, "observe nonzero pipe capacity");
    return filled;
}

auto backpressure_body(
    PipeQueue& queue,
    Pipe& pipe,
    std::size_t capacity,
    bool& released,
    Observation& observed
) noexcept -> async::Operation<void> {
    const auto data = std::vector<std::byte>(capacity + 1, std::byte {13});
    auto filled = capacity;
    for (;;) {
        const auto count = ::write(pipe.writer(), data.data(), data.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            check(
                errno == EAGAIN || errno == EWOULDBLOCK,
                "the exact requested write reaches real backpressure"
            );
            break;
        }
        check(count > 0, "prepare exact write request makes progress");
        filled += static_cast<std::size_t>(count);
    }
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(write(queue, pipe.writer(), data, observed));
    co_await async::await_yield_once();
    check(
        queue.pending_count() == 1 && observed.finished == 0,
        "full pipe write suspends on EAGAIN with live buffer"
    );
    auto discard = std::array<std::byte, 4096> {};
    auto offset = std::size_t(0);
    while (offset != filled) {
        const auto remaining = filled - offset;
        const auto width = remaining < discard.size() ? remaining : discard.size();
        const auto count = raw_read(pipe.reader(), std::span(discard).first(width));
        check(count > 0, "drain filled pipe makes progress");
        offset += count;
    }
    auto drained = std::vector<std::byte>(data.size());
    released = true;
    auto result = co_await child.observe();
    auto transferred = std::size_t(0);
    auto count = bytes(result);
    for (;;) {
        check(
            count > 0 && count <= data.size() - transferred,
            "write_some reports actual progress within requested byte count"
        );
        offset = 0;
        while (offset != count) {
            const auto received = raw_read(
                pipe.reader(),
                std::span(drained).subspan(transferred + offset, count - offset)
            );
            check(received > 0, "read transferred bytes makes progress");
            offset += received;
        }
        transferred += count;
        if (transferred == data.size()) {
            break;
        }
        auto next = co_await queue.write_some(pipe.writer(), std::span(data).subspan(transferred));
        count = bytes(next);
    }
    check(drained == data, "caller sends remaining bytes and preserves the complete byte sequence");
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

auto backpressure() noexcept -> void {
    auto pipe = Pipe();
    const auto capacity = fill(pipe);
    auto queue = PipeQueue();
    auto released = false;
    auto pump = PollGate(queue, released);
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result =
        async::drive_root(backpressure_body(queue, pipe, capacity, released, observed), &pump);
    check(
        result.success_if() != nullptr && observed.finished == 1 && queue.registration_count() == 1,
        "registered backpressure writer completes once"
    );
    balanced(queue);
}

} // namespace

auto main() -> int {
    for (const auto& test : {
             std::pair {"cold", cold},
             std::pair {"transfer", transfer},
             std::pair {"eof-error", eof_error},
             std::pair {"delayed", delayed},
             std::pair {"idle-wait", idle_wait},
             std::pair {"cancelled-close", cancelled_close},
             std::pair {"committed", committed},
             std::pair {"rearm", rearm},
             std::pair {"backpressure", backpressure},
         }) {
        test.second();
        std::printf("%s: pass\n", test.first);
    }
    return EXIT_SUCCESS;
}
