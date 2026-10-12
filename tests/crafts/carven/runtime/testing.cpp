#include <carven/runtime/testing/testing.hpp>

auto testing_header_contract() noexcept -> int {
    auto context = carven::runtime::TestContext();
    context.begin_case("Runtime", "testing");
    context.end_case();
    return context.result();
}
