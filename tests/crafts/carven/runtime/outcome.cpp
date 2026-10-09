#include <carven/runtime/outcome.hpp>

namespace {

struct Failure final {};

struct OtherFailure final {};

} // namespace

auto outcome_header_contract() noexcept -> bool {
    using Narrow = carven::runtime::Outcome<int, Failure>;
    using Wide = carven::runtime::Outcome<int, Failure, OtherFailure>;
    auto success = Narrow::success_from([]() noexcept { return 42; });
    auto failure = Wide(Narrow::failure(Failure {}));
    return success.success_if() != nullptr && failure.failure_if<Failure>() != nullptr;
}
