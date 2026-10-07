#include "transfer.hpp"
#include <carven/api/tests/interop/async/bridge.hpp>
#include <carven/api/tests/interop/async/transfer.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <numeric>
#include <thread>
#include <unistd.h>

namespace {
namespace async = carven::runtime::async;
namespace api = carven::api::tests::interop::async::transfer;
using async_transfer::Context;
using async_interop::PipeQueue;
using IoError = decltype(carven::api::tests::interop::async::bridge::make_io_error(0));
const auto data = std::array {
    std::byte {0},
    std::byte {255},
    std::byte {1},
    std::byte {3},
    std::byte {5},
    std::byte {7},
    std::byte {9}
};

auto check(bool valid, const char* message) noexcept -> void {
    if (!valid) {
        std::fprintf(stderr, "async transfer contract failed: %s (errno=%d)\n", message, errno);
        std::exit(EXIT_FAILURE);
    }
}

class Pipe final {
    int descriptors[2];

public:
    Pipe() noexcept {
        check(::pipe(descriptors) == 0, "create pipe");
        for (const auto fd : descriptors) {
            const auto flags = ::fcntl(fd, F_GETFL);
            check(flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0, "nonblocking fd");
        }
    }

    Pipe(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    auto operator=(const Pipe&) -> Pipe& = delete;
    auto operator=(Pipe&&) -> Pipe& = delete;

    ~Pipe() noexcept {
        for (const auto fd : descriptors) {
            if (fd >= 0) {
                check(::close(fd) == 0, "close fd");
            }
        }
    }

    auto reader() const noexcept -> int { return descriptors[0]; }

    auto writer() const noexcept -> int { return descriptors[1]; }

    auto close_writer() noexcept -> void {
        check(::close(descriptors[1]) == 0, "close writer for EOF");
        descriptors[1] = -1;
    }
};

auto write_all(int fd, std::span<const std::byte> bytes) noexcept -> void {
    while (!bytes.empty()) {
        ssize_t count;
        do {
            count = ::write(fd, bytes.data(), bytes.size());
        } while (count < 0 && errno == EINTR);
        check(count > 0, "raw write progresses");
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
}

auto read_exact(int fd, std::span<std::byte> bytes) noexcept -> void {
    while (!bytes.empty()) {
        ssize_t count;
        do {
            count = ::read(fd, bytes.data(), bytes.size());
        } while (count < 0 && errno == EINTR);
        check(count > 0, "raw read progresses");
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
}

class Transfer final {
public:
    Pipe input;
    Pipe output;
    PipeQueue queue;
    std::array<std::byte, 4> read_buffer {};
    std::array<std::byte, 4> write_buffer {};
    async_bridge::Observations reading {};
    async_bridge::Observations writing {};
    std::vector<std::size_t> read_counts;
    std::vector<std::size_t> write_counts;
    Context reader;
    Context writer;

    explicit Transfer(bool invalid_write = false) noexcept
        : reader(queue, input.reader(), read_buffer, reading, read_counts),
          writer(
              queue,
              invalid_write ? output.reader() : output.writer(),
              write_buffer,
              writing,
              write_counts
          ) {}

    auto preload() noexcept -> void {
        write_all(input.writer(), data);
        input.close_writer();
    }

    auto balanced() const noexcept -> void {
        check(
            queue.pending_count() == 0 && queue.registration_count() == queue.completion_count(),
            "all registrations settle before host cleanup"
        );
        for (const auto* observed : {&reading, &writing}) {
            check(
                observed->live == 0
                    && observed->started == observed->finished
                    && observed->started == observed->destroyed
                    && observed->context_destroyed == 0,
                "provider loans settle once while host holders remain alive"
            );
        }
    }

    auto copied(std::uint64_t count) noexcept -> void {
        check(count == data.size(), "source accumulates actual byte count");
        auto received = std::array<std::byte, data.size()> {};
        read_exact(output.reader(), received);
        check(received == data, "chunk cursor preserves all bytes including zero and 255");
        check(!read_counts.empty() && read_counts.back() == 0, "source stops on actual EOF");
        check(
            std::accumulate(read_counts.begin(), read_counts.end(), std::size_t {0}) == data.size()
                && std::accumulate(write_counts.begin(), write_counts.end(), std::size_t {0})
                    == data.size(),
            "read and write counts account for complete payload"
        );
        check(
            std::any_of(
                read_counts.begin(),
                read_counts.end(),
                [this](std::size_t n) { return n > 0 && n < read_buffer.size(); }
            ),
            "source handles partial read"
        );
        balanced();
    }
};

auto chunks() noexcept -> void {
    auto transfer = Transfer();
    transfer.preload();
    {
        auto cold = api::copy(transfer.reader, transfer.writer);
    }
    check(
        transfer.reading.started == 0
            && transfer.writing.started == 0
            && transfer.queue.registration_count() == 0,
        "cold copy does not touch provider or buffers"
    );
    auto result = async::drive_root(api::copy(transfer.reader, transfer.writer), &transfer.queue);
    check(result.success_if() != nullptr, "ready chunk copy succeeds");
    transfer.copied(result.success_if()->value);
}

class ExternalInput final {
    Transfer& transfer;
    std::thread writer;
    bool injected = false;

public:
    explicit ExternalInput(Transfer& value) noexcept
        : transfer(value) {}

    ~ExternalInput() noexcept {
        if (writer.joinable()) {
            writer.join();
        }
    }

    auto poll(async::Driver& driver) noexcept -> void { transfer.queue.poll(driver); }

    auto wait() noexcept -> bool {
        check(
            !injected
                && transfer.queue.pending_count() == 1
                && transfer.reading.live == 1
                && transfer.writing.live == 0,
            "source read registers while borrowed buffer remains live"
        );
        // The thread touches only host-owned fd, never queue or Driver.
        injected = true;
        writer = std::thread([this] {
            write_all(transfer.input.writer(), data);
            transfer.input.close_writer();
        });
        const auto ready = transfer.queue.wait();
        writer.join(); // EOF is established before the source resumes and reads again.
        return ready;
    }
};

auto pending_read() noexcept -> void {
    auto transfer = Transfer();
    auto input = ExternalInput(transfer);
    auto result = async::drive_root(api::copy(transfer.reader, transfer.writer), &input);
    check(result.success_if() != nullptr, "pending chunk read resumes through actual poll");
    transfer.copied(result.success_if()->value);
    check(
        transfer.queue.registration_count() == 1 && transfer.queue.wait_count() == 1,
        "one empty-input registration and one idle wait"
    );
}

class DrainOutput final {
    Transfer& transfer;
    std::size_t filled;
    bool drained = false;

public:
    DrainOutput(Transfer& value, std::size_t initial) noexcept
        : transfer(value),
          filled(initial) {}

    auto poll(async::Driver& driver) noexcept -> void { transfer.queue.poll(driver); }

    auto wait() noexcept -> bool {
        check(
            !drained
                && transfer.queue.pending_count() == 1
                && transfer.writing.live == 1
                && transfer.reading.live == 0,
            "full-output write holds buffer until terminal delivery"
        );
        auto block = std::array<std::byte, 256> {};
        while (filled > 0) {
            const auto amount = std::min(filled, block.size());
            read_exact(transfer.output.reader(), std::span(block).first(amount));
            check(
                std::all_of(
                    block.begin(),
                    block.begin() + amount,
                    [](std::byte b) { return b == std::byte {85}; }
                ),
                "preload bytes remain intact"
            );
            filled -= amount;
        }
        drained = true;
        return transfer.queue.wait();
    }
};

auto backpressure() noexcept -> void {
    auto transfer = Transfer();
    transfer.preload();
    const auto block = std::array {std::byte {85}, std::byte {85}, std::byte {85}, std::byte {85}};
    std::size_t filled = 0;
    for (;;) {
        ssize_t count;
        do {
            count = ::write(transfer.output.writer(), block.data(), block.size());
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            check(errno == EAGAIN || errno == EWOULDBLOCK, "fill reaches actual EAGAIN");
            break;
        }
        check(count > 0, "fill progresses");
        filled += static_cast<std::size_t>(count);
    }
    // The next source write has the same 4-byte request and unchanged pipe state.
    auto output = DrainOutput(transfer, filled);
    auto result = async::drive_root(api::copy(transfer.reader, transfer.writer), &output);
    check(result.success_if() != nullptr, "source write resumes after host drains capacity");
    transfer.copied(result.success_if()->value);
    check(
        transfer.queue.registration_count() == 1 && transfer.queue.wait_count() == 1,
        "one EAGAIN write registration is unlinked and delivered"
    );
}

auto failure() noexcept -> void {
    for (const auto recovered : {false, true}) {
        auto transfer = Transfer(true);
        transfer.preload();
        if (recovered) {
            auto result =
                async::drive_root(api::recover(transfer.reader, transfer.writer), &transfer.queue);
            check(
                result.success_if() != nullptr && result.success_if()->value == EBADF,
                "source catches actual write error after successful read"
            );
        } else {
            auto result =
                async::drive_root(api::copy(transfer.reader, transfer.writer), &transfer.queue);
            const auto* error = result.failure_if<IoError>();
            check(
                error != nullptr && error->code == EBADF,
                "source propagates typed actual write error"
            );
        }
        check(
            transfer.read_counts.size() == 1
                && transfer.read_counts.front() > 0
                && transfer.write_counts.empty(),
            "error does not roll back consumed input"
        );
        transfer.balanced();
    }
}

auto cancelled_close() noexcept -> void {
    auto transfer = Transfer();
    auto result =
        async::drive_root(api::cancel_copy(transfer.reader, transfer.writer), &transfer.queue);
    check(
        result.success_if() != nullptr && result.success_if()->value == 17,
        "source child cancellation closes before parent delivery"
    );
    check(
        transfer.queue.registration_count() == 1
            && transfer.queue.completion_count() == 1
            && transfer.reading.cancelled == 1
            && transfer.writing.started == 0
            && transfer.read_counts.empty()
            && transfer.write_counts.empty(),
        "cancelled pending read releases registration without accessing write buffer"
    );
    transfer.balanced();
}
} // namespace

int main() {
    chunks();
    std::puts("chunks: pass");
    pending_read();
    std::puts("pending-read: pass");
    backpressure();
    std::puts("backpressure: pass");
    failure();
    std::puts("failure: pass");
    cancelled_close();
    std::puts("cancelled-close: pass");
}
