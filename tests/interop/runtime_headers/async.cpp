#include <carven/runtime/async/async.hpp>

namespace {
auto cold() noexcept -> carven::runtime::async::Operation<void> {
    co_return carven::runtime::async::Completion<void>::success();
}
} // namespace

auto async_header_contract() noexcept -> bool {
    auto continuation = carven::runtime::async::ResumeContinuation();
    auto completion = carven::runtime::async::drive_root(cold());
    return !continuation.bound() && completion.success_if() != nullptr;
}
