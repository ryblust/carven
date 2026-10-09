#pragma once

#include <cstdint>
#include <limits>
#include <utility>

namespace carven::runtime::execution {

namespace detail {

template<typename Result, typename Timeout>
auto timeout_timer(std::int64_t deadline) noexcept -> AsyncOperation<Result, Timeout> {
    const auto completion = co_await sleep_until(deadline);
    if (completion.is_cancelled()) {
        co_return Completion<Result, Timeout>::cancelled();
    }
    require_protocol(completion.success_if() != nullptr);
    co_return Completion<Result, Timeout>::failure(Timeout {});
}

} // namespace detail

template<
    typename Result,
    typename Timeout,
    typename RangeError,
    typename... Failures,
    typename Input>
auto timeout(Input input, std::int64_t duration) noexcept -> AsyncOperation<Result, Failures...> {
    using Output = Completion<Result, Failures...>;
    if (cancellation_requested()) {
        co_return Output::cancelled();
    }
    if (duration <= 0) {
        co_return Output::failure(Timeout {});
    }
    const auto instant = now();
    if (instant > std::numeric_limits<std::int64_t>::max() - duration) {
        co_return Output::failure(RangeError {});
    }
    auto timer = detail::timeout_timer<Result, Timeout>(instant + duration);
    co_return co_await compose<CompositionPolicy::ForwardFirst, Result, Failures...>(
        std::move(input),
        std::move(timer)
    );
}

} // namespace carven::runtime::execution
