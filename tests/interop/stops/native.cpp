#include <carven/api/tests/interop/stops/callback.hpp>
#include <carven/generated/carven-test-runner.hpp>
#include <carven/providers/tests/interop/stops/callback.hpp>

#include <cstdlib>

namespace carven::providers::tests::interop::stops::callback::cv_escaped_functions::call_guarded {

auto invoke() noexcept -> Completion {
    return carven::api::tests::interop::stops::callback::guarded(false);
}

} // namespace carven::providers::tests::interop::stops::callback::cv_escaped_functions::call_guarded

auto run_stop_case() noexcept -> int {
    return carven::runtime::run_generated_tests() == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
}
