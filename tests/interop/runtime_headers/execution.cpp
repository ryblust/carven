#include <carven/runtime/execution/execution.hpp>

namespace {
auto cold() noexcept -> carven::runtime::execution::AsyncOperation<void> {
    co_return carven::runtime::execution::Completion<void>::success();
}
} // namespace

auto execution_header_contract() noexcept -> bool {
    auto continuation = carven::runtime::execution::ResumeContinuation();
    auto completion = carven::runtime::execution::drive_root(cold());
    auto waited = carven::runtime::execution::drive_root(
        carven::runtime::execution::sleep_until(carven::runtime::execution::now())
    );
    auto joined = carven::runtime::execution::drive_root(
        carven::runtime::execution::when_all<carven::runtime::Tuple<carven::runtime::Void>>(cold())
    );
    auto raced = carven::runtime::execution::drive_root(
        carven::runtime::execution::when_any<
            carven::runtime::Tuple<std::size_t, carven::runtime::Void>>(cold())
    );
    auto first = carven::runtime::execution::drive_root(
        carven::runtime::execution::first_successful<
            carven::runtime::Tuple<std::size_t, carven::runtime::Void>>(cold())
    );
    return !continuation.bound()
        && completion.success_if() != nullptr
        && waited.success_if() != nullptr
        && joined.success_if() != nullptr
        && raced.success_if() != nullptr
        && first.success_if() != nullptr;
}
