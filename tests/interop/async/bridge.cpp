#include "bridge_provider.hpp"

#include <carven/api/tests/interop/async/bridge.hpp>
#include <carven/runtime/execution/child.hpp>

#include <array>
#include <string_view>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

auto run_transfer_cases() noexcept -> void;

namespace {

namespace async = carven::runtime::execution;
namespace api = carven::api::tests::interop::async::bridge;

using async_bridge::Context;
using async_bridge::Observations;
using async_bridge::TrivialContext;
using async_interop::PipeQueue;
using IOError = async_bridge::providers::bridge_read_byte::Failure0;

auto check(bool condition, const char* message) noexcept -> void {
    if (!condition) {
        std::fprintf(stderr, "async bridge contract failed: %s (errno=%d)\n", message, errno);
        std::exit(EXIT_FAILURE);
    }
}

auto observations() noexcept -> Observations {
    return {
        .started = 0,
        .finished = 0,
        .cancelled = 0,
        .live = 0,
        .destroyed = 0,
        .context_destroyed = 0,
        .trace = 0
    };
}

auto balanced(const PipeQueue& queue, const Observations& observed) noexcept -> void {
    check(queue.pending_count() == 0, "provider registrations are fully closed");
    check(
        observed.live == 0
            && observed.started == observed.finished
            && observed.started == observed.destroyed,
        "every executing native buffer is cleaned up exactly once"
    );
}

class Pipe final {
    int descriptors[2];

public:
    Pipe() noexcept {
        check(::pipe(descriptors) == 0, "create pipe");
        for (const auto fd : descriptors) {
            const auto flags = ::fcntl(fd, F_GETFL);
            check(flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0, "nonblocking pipe");
        }
    }

    Pipe(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    auto operator=(const Pipe&) -> Pipe& = delete;
    auto operator=(Pipe&&) -> Pipe& = delete;

    ~Pipe() noexcept {
        for (const auto fd : descriptors) {
            if (fd >= 0) {
                check(::close(fd) == 0, "close owned fd");
            }
        }
    }

    auto reader() const noexcept -> int { return descriptors[0]; }

    auto writer() const noexcept -> int { return descriptors[1]; }

    auto close_writer() noexcept -> void {
        check(::close(descriptors[1]) == 0, "close write end");
        descriptors[1] = -1;
    }
};

auto put(int fd, std::byte byte) noexcept -> void {
    ssize_t count;
    do {
        count = ::write(fd, &byte, 1);
    } while (count < 0 && errno == EINTR);
    check(count == 1, "write real pipe byte");
}

auto cold() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto observed = observations();
    {
        auto context = Context(queue, pipe.reader(), observed);
        {
            auto operation = api::source_read(context);
            check(observed.started == 0, "exported source root remains cold");
        }
        balanced(queue, observed);
    }
    check(observed.context_destroyed == 1, "cold drop does not retain or destroy host context");
}

auto ready() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto received = observations();
    auto sent = observations();
    auto reader = Context(queue, pipe.reader(), received);
    auto writer = Context(queue, pipe.writer(), sent);
    auto result = async::drive_root(api::source_exchange(reader, writer, 255), &queue);
    check(
        result.success_if() != nullptr && result.success_if()->value == 255,
        "generated source awaits native write/read with exact data"
    );
    put(pipe.writer(), std::byte {41});
    auto stored_observed = observations();
    auto stored_reader = Context(queue, pipe.reader(), stored_observed);
    auto stored = async::drive_root(api::source_stored(stored_reader), &queue);
    check(
        stored.success_if() != nullptr
            && stored.success_if()->value == 41
            && stored_observed.trace == 123,
        "stored native producer stays cold until generated await"
    );
    pipe.close_writer();
    auto eof = async::drive_root(api::source_read(reader), &queue);
    check(
        eof.success_if() != nullptr && eof.success_if()->value == -1,
        "native EOF passes source await as scalar sentinel"
    );
    balanced(queue, received);
    balanced(queue, sent);
    balanced(queue, stored_observed);
}

class ExternalWriter final : public async::EventSource {
    PipeQueue& queue;
    int fd;
    const Observations& observed;
    bool waited = false;

public:
    ExternalWriter(PipeQueue& provider, int descriptor, const Observations& state) noexcept
        : queue(provider),
          fd(descriptor),
          observed(state) {}

    auto poll(async::Driver& driver) noexcept -> void override {
        if (waited || queue.pending_count() == 0) {
            queue.poll(driver);
        }
    }

    auto wait(std::optional<std::int64_t> limit) noexcept -> bool override {
        check(
            !waited && queue.pending_count() == 1 && observed.live == 1 && observed.finished == 0,
            "generated await reaches real pending I/O with native frame buffer alive"
        );
        waited = true;
        auto writer =
            std::thread([descriptor = fd]() noexcept { put(descriptor, std::byte {57}); });
        const auto pending = queue.wait(limit);
        writer.join();
        return pending;
    }
};

auto pending() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto observed = observations();
    auto context = Context(queue, pipe.reader(), observed);
    auto source = ExternalWriter(queue, pipe.writer(), observed);
    auto result = async::drive_root(api::source_read(context), &source);
    check(
        result.success_if() != nullptr && result.success_if()->value == 57,
        "host external event resumes native producer and generated source consumer"
    );
    balanced(queue, observed);
}

auto failures() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto observed = observations();
    auto context = Context(queue, pipe.writer(), observed);
    auto result = async::drive_root(api::source_read(context), &queue);
    const auto* error = result.failure_if<IOError>();
    check(
        error != nullptr && error->code == EBADF,
        "native errno maps to canonical source failure and propagates to host"
    );
    auto recovered = async::drive_root(api::source_recover(context), &queue);
    check(
        recovered.success_if() != nullptr && recovered.success_if()->value == EBADF,
        "generated typed catch handles native provider failure"
    );
    balanced(queue, observed);
}

auto cancelled_close_case(int mode) noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto observed = observations();
    {
        auto context = Context(queue, pipe.reader(), observed);
        auto result = async::drive_root(
            mode == 0       ? api::source_close(context)
                : mode == 1 ? api::source_explicit_close(context)
                            : api::source_drain(context),
            &queue
        );
        check(
            result.success_if() != nullptr && result.success_if()->value == 17,
            "generated lexical closure returns only after child cancellation settles"
        );
        check(
            observed.started == 1 && observed.cancelled == 1 && observed.context_destroyed == 0,
            "source cancellation unlinks pending I/O without releasing borrowed context"
        );
        balanced(queue, observed);
    }
    check(observed.context_destroyed == 1, "host releases context after source closure");
}

auto cancelled_close() noexcept -> void {
    cancelled_close_case(0);
    cancelled_close_case(1);
    cancelled_close_case(2);
}

auto trivial() noexcept -> void {
    auto pipe = Pipe();
    auto queue = PipeQueue();
    auto observed = observations();
    auto context = TrivialContext {
        .queue = &queue,
        .fd = pipe.reader(),
        .observed = &observed,
        .host_address = nullptr
    };
    context.host_address = &context;
    put(pipe.writer(), std::byte {67});
    auto result = async::drive_root(api::source_trivial(context), &queue);
    check(
        result.success_if() != nullptr && result.success_if()->value == 67,
        "trivial native holder is borrowed through synchronous factory and stored operation"
    );
    balanced(queue, observed);
}

auto cancel_character() noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(api::source_character(0xd800, false));
    async::cancel(child);
    auto result = co_await child.observe();
    check(
        result.is_cancelled(),
        "cancelled char completion never reads or validates absent success"
    );
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

auto cancel_join_characters() noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(api::source_join_characters(0xd800, false));
    async::cancel(child);
    auto result = co_await child.observe();
    check(result.is_cancelled(), "cancelled char composition never reads absent success");
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

auto characters() noexcept -> void {
    auto result = async::drive_root(api::source_character(90, false));
    check(
        result.success_if() != nullptr && result.success_if()->value == U'Z',
        "native char success is delivered as valid scalar"
    );
    auto discarded = async::drive_root(api::source_discard_character(90, false));
    check(
        discarded.success_if() != nullptr,
        "discarded native char await still executes valid success"
    );
    auto failed = async::drive_root(api::source_character(0xd800, true));
    const auto* error = failed.failure_if<IOError>();
    check(
        error != nullptr && error->code == EIO,
        "char failure is handled without reading success"
    );
    auto cancelled = async::drive_root(cancel_character());
    check(cancelled.success_if() != nullptr, "char cancellation preserves terminal channel");
    auto joined = async::drive_root(api::source_join_characters(90, false));
    auto raced = async::drive_root(api::source_race_characters(90, false));
    auto first = async::drive_root(api::source_first_character(90, false));
    check(
        joined.success_if() && raced.success_if() && first.success_if(),
        "compositions validate valid character successes"
    );
    auto failed_join = async::drive_root(api::source_join_characters(0xd800, true));
    auto failed_race = async::drive_root(api::source_race_characters(0xd800, true));
    auto recovered_first = async::drive_root(api::source_first_character(0xd800, true));
    check(
        failed_join.failure_if<IOError>()
            && failed_race.failure_if<IOError>()
            && recovered_first.success_if(),
        "compositions do not validate the absent success of a failure"
    );
    auto cancelled_join = async::drive_root(cancel_join_characters());
    check(cancelled_join.success_if() != nullptr, "char composition preserves cancellation");
}

} // namespace

auto run_bridge_cases() noexcept -> void {
    for (const auto& test : {
             std::pair {"cold", cold},
             std::pair {"ready", ready},
             std::pair {"pending", pending},
             std::pair {"failures", failures},
             std::pair {"cancelled-close", cancelled_close},
             std::pair {"trivial", trivial},
             std::pair {"characters", characters},
         }) {
        test.second();
        std::printf("%s: pass\n", test.first);
    }
    run_transfer_cases();
}

auto run_invalid_character(std::string_view name) noexcept -> int {
    if (std::signal(SIGABRT, +[](int) { std::_Exit(EXIT_SUCCESS); }) == SIG_ERR) {
        return EXIT_FAILURE;
    }
    if (name == "invalidchar") {
        static_cast<void>(async::drive_root(api::source_character(0xd800, false)));
    } else if (name == "invalidchar-discard") {
        static_cast<void>(async::drive_root(api::source_discard_character(0xd800, false)));
    } else if (name == "invalidchar-join") {
        static_cast<void>(async::drive_root(api::source_join_characters(0xd800, false)));
    } else if (name == "invalidchar-race") {
        static_cast<void>(async::drive_root(api::source_race_characters(0xd800, false)));
    } else if (name == "invalidchar-first") {
        static_cast<void>(async::drive_root(api::source_first_character(0xd800, false)));
    } else if (name == "invalidchar-join-later-failure") {
        static_cast<void>(async::drive_root(api::source_join_character_then_failure(0xd800)));
    }
    return EXIT_FAILURE;
}
