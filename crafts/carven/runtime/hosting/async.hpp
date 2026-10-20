#pragma once

#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/hosting/service.hpp>

namespace carven::runtime::hosting {

namespace detail {

// Each awaiting coroutine temporarily registers its own source with its driver.
// Completion only queues the continuation; providers never resume native frames.
template<typename Completion>
class Awaiter final : public carven::runtime::execution::EventSource {
public:
    explicit Awaiter(std::unique_ptr<Pending<Completion>> pending) noexcept
        : pending(std::move(pending)),
          continuation(),
          completion(std::nullopt) {}

    auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> frame) noexcept -> bool {
        continuation.bind(frame);
        if (!pending) {
            completion.emplace(Completion::stopped());
            return false;
        }
        if (continuation.cancel_requested()) {
            pending->cancel();
            pending.reset();
            completion.emplace(Completion::cancelled());
            return false;
        }
        continuation.driver().attach(*this);
        return true;
    }

    auto await_resume() noexcept -> Completion {
        carven::runtime::execution::detail::require_protocol(completion.has_value());
        return std::move(*completion);
    }

    auto poll(carven::runtime::execution::Driver& driver) noexcept -> void override {
        if (continuation.cancel_requested()) {
            pending->cancel();
            finish(Completion::cancelled(), driver);
            return;
        }
        auto response = pending->poll();
        if (!response) {
            return;
        }
        finish(std::move(*response), driver);
    }

    auto wait(std::optional<std::int64_t>) noexcept -> bool override { return false; }

private:
    std::unique_ptr<Pending<Completion>> pending;
    carven::runtime::execution::ResumeContinuation continuation;
    std::optional<Completion> completion;

    auto finish(Completion result, carven::runtime::execution::Driver& driver) noexcept -> void {
        if (result.is_stopped()) {
            continuation.request_stop();
        }
        driver.detach(*this);
        pending.reset();
        completion.emplace(std::move(result));
        continuation.enqueue(driver);
    }
};

} // namespace detail

template<
    ServiceName Name,
    typename Result,
    typename NativeDeclarations,
    typename... Arguments,
    typename... Failures>
auto AsyncService<Name, Result(Arguments...), NativeDeclarations, Failures...>::operator()(
    Arguments... arguments
) const noexcept -> carven::runtime::execution::AsyncOperation<NativeResult, Failures...> {
    return run(*this, std::forward<Arguments>(arguments)...);
}

template<
    ServiceName Name,
    typename Result,
    typename NativeDeclarations,
    typename... Arguments,
    typename... Failures>
auto AsyncService<Name, Result(Arguments...), NativeDeclarations, Failures...>::run(
    AsyncService service,
    std::remove_cvref_t<Arguments>... arguments
) noexcept -> carven::runtime::execution::AsyncOperation<NativeResult, Failures...> {
    co_return co_await detail::Awaiter<Completion>(
        service.start(static_cast<Arguments&&>(arguments)...)
    );
}

} // namespace carven::runtime::hosting
