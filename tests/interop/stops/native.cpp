#include "native.hpp"

#include <carven/api/tests/interop/stops/callback.hpp>
#include <carven/generated/carven-test-runner.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>

auto call_guarded() noexcept -> void {
    static_cast<void>(carven::api::tests::interop::stops::callback::guarded(false));
    std::fputs("unreachable native continuation\n", stderr);
}

auto run_stop_case() noexcept -> int {
    if (std::signal(SIGABRT, +[](int) { std::_Exit(EXIT_SUCCESS); }) == SIG_ERR) {
        return EXIT_FAILURE;
    }
    static_cast<void>(carven::runtime::run_generated_tests());
    return EXIT_FAILURE;
}
