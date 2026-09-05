#include <carven/std/testing/testing.hpp>

auto testing_header_contract() noexcept -> int {
    auto context = carven::testing::TestContext();
    context.begin_case("runtime_headers", "testing");
    context.end_case();
    return context.result();
}
