#if defined(_MSC_VER)
#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif

#include <carven/api/tests/interop/exceptions/precomputed.hpp>

#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/callable.hpp>
#include <carven/runtime/outcome.hpp>
#include <carven/runtime/text/text.hpp>
#include <carven/runtime/format.hpp>
#include <carven/runtime/print.hpp>

#include "provider.hpp"

#include <csignal>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <utility>

namespace {

namespace async = carven::runtime::execution;

volatile std::sig_atomic_t async_case = 0;
volatile std::sig_atomic_t async_boundary_reached = 0;

auto terminated() noexcept -> void {
    // An unrelated early termination cannot establish the async boundary.
    std::_Exit(!async_case || async_boundary_reached ? 73 : 76);
}

auto aborted(int signal) noexcept -> void {
    if (signal == SIGABRT) {
        terminated();
    }
    std::_Exit(74);
}

class Foreign final {
public:
    bool fail;

    explicit Foreign(bool source) noexcept
        : fail(source) {}

    Foreign(const Foreign&) { throw 1; }

    Foreign(Foreign&& source) noexcept(false)
        : fail(source.fail) {
        if (fail) {
            throw 2;
        }
    }

    auto operator=(const Foreign&) -> Foreign& = delete;
    auto operator=(Foreign&&) -> Foreign& = delete;
};

auto foreign_function() -> int {
    throw 3;
}

auto throw_async_exception() -> void {
    async_boundary_reached = true;
    throw 5;
}

auto async_body() noexcept -> async::AsyncOperation<void> {
    throw_async_exception();
    co_return async::Completion<void>::success();
}

class ThrowingAwaiter final {
public:
    auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<>) const -> void { throw_async_exception(); }

    auto await_resume() const noexcept -> void {}
};

auto async_awaiter() noexcept -> async::AsyncOperation<void> {
    co_await ThrowingAwaiter();
    co_return async::Completion<void>::success();
}

class AsyncPayload final {
public:
    bool fail;

    explicit AsyncPayload(bool source)
        : fail(source) {
        if (fail) {
            throw_async_exception();
        }
    }

    AsyncPayload(const AsyncPayload&) = delete;

    AsyncPayload(AsyncPayload&& source)
        : fail(source.fail) {
        if (fail) {
            throw_async_exception();
        }
    }

    auto operator=(const AsyncPayload&) -> AsyncPayload& = delete;
    auto operator=(AsyncPayload&&) -> AsyncPayload& = delete;
};

} // namespace

// This process is built with C++ exceptions enabled to test the protocol boundary.
// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) -> int try {
    std::set_terminate(&terminated);
    std::signal(SIGABRT, aborted);
    if (argc != 2) {
        return 1;
    }
    const auto operation = std::string_view(argv[1]);
    async_case = operation.starts_with("async-");
    if (operation.starts_with("print-")) {
        // Termination uses _Exit; expose each completed write to the capture file.
        if (std::setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
            return 3;
        }
    }
    using Outcome = carven::runtime::Outcome<Foreign, Foreign>;
    if (operation == "async-body") {
        static_cast<void>(async::drive_root(async_body()));
    } else if (operation == "async-awaiter") {
        static_cast<void>(async::drive_root(async_awaiter()));
    } else if (operation == "async-success") {
        static_cast<void>(async::Completion<AsyncPayload>::success_from([]() {
            return AsyncPayload(true);
        }));
    } else if (operation == "copy") {
        const auto source = Foreign(false);
        static_cast<void>(Outcome::success_from([&]() { return Foreign(source); }));
    } else if (operation == "move") {
        auto source = Outcome::success_from([]() noexcept { return Foreign(false); });
        source.success_if()->value.fail = true;
        static_cast<void>(Outcome(std::move(source)));
    } else if (operation == "function") {
        const auto view = carven::runtime::FunctionRef<int() noexcept>(&foreign_function);
        static_cast<void>(view());
    } else if (operation == "format-throw" || operation == "format-utf8") {
        static_cast<void>(carven::runtime::format("{}", FormatProbe {operation == "format-utf8"}));
    } else if (operation == "append-format-throw") {
        carven::api::tests::interop::exceptions::precomputed::append_native_failure(false);
    } else if (operation == "print-inner-throw") {
        carven::api::tests::interop::exceptions::precomputed::print_native_failure(false);
    } else if (operation == "print-later-throw") {
        carven::runtime::println(std::string_view("42"), FormatProbe {.invalid_utf8 = false});
    }
    return 2;
} catch (...) {
    // Escaping the runtime boundary is a failure, even though uncaught exceptions terminate.
    return 75;
}
