#include <carven/runtime/testing.hpp>

auto testing_header_contract() noexcept -> int {
    auto context = carven::runtime::TestContext();
    context.begin_case("runtime_headers", "testing");
    context.end_case();
    return context.result();
}
