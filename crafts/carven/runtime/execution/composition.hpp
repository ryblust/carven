#pragma once

#include <carven/runtime/execution/child.hpp>
#include <carven/runtime/text/utf8.hpp>

#include <optional>
#include <type_traits>
#include <utility>

namespace carven::runtime::execution {

enum class CompositionPolicy { WhenAll, WhenAny, FirstSuccessful, ForwardFirst };

namespace detail {

enum class CandidateState { Success, Failure, Cancelled };

struct UncheckedCompositionSite final {};

template<typename Input, bool Validate>
struct CompositionOperand final {
    Input operation;
    carven::runtime::SourceSite site;
};

template<typename Input>
struct CompositionInput;

template<typename Result, typename... Failures>
struct CompositionInput<AsyncOperation<Result, Failures...>> final {
    using Value = std::conditional_t<std::is_void_v<Result>, carven::runtime::Void, Result>;
    using ChildType = Child<Result, Failures...>;
    using CompletionType = Completion<Result, Failures...>;
    static constexpr bool validate = false;
    using ValidationSite = UncheckedCompositionSite;

    static auto operation(AsyncOperation<Result, Failures...>& input) noexcept
        -> AsyncOperation<Result, Failures...>& {
        return input;
    }

    static auto site(AsyncOperation<Result, Failures...>&) noexcept -> ValidationSite { return {}; }
};

template<typename Input, bool Validate>
struct CompositionInput<CompositionOperand<Input, Validate>> final {
    using Base = CompositionInput<Input>;
    using Value = typename Base::Value;
    using ChildType = typename Base::ChildType;
    using CompletionType = typename Base::CompletionType;
    static constexpr bool validate = Validate;
    using ValidationSite =
        std::conditional_t<Validate, carven::runtime::SourceSite, UncheckedCompositionSite>;

    static auto operation(CompositionOperand<Input, Validate>& input) noexcept -> Input& {
        return input.operation;
    }

    static auto site(CompositionOperand<Input, Validate>& input) noexcept -> ValidationSite {
        if constexpr (Validate) {
            return input.site;
        } else {
            return {};
        }
    }
};

template<typename Output, typename Result, typename... Failures>
auto forward_nominal(Completion<Result, Failures...>& source) noexcept -> Output {
    auto output = std::optional<Output>();
    (
        [&]() noexcept {
            if (auto* failure = source.template failure_if<Failures>()) {
                output.emplace(Output::failure(carven::runtime::transfer(*failure)));
            }
        }(),
        ...);
    require_protocol(output.has_value());
    return std::move(*output);
}

template<typename... Inputs>
struct CompositionSlots;

template<>
struct CompositionSlots<> final {
    explicit CompositionSlots(ChildScope&) noexcept {}

    template<std::size_t Count>
    auto fill(std::array<ChildSlot*, Count>&, std::size_t) noexcept -> void {}

    template<std::size_t Count>
    auto cancel_active(const std::array<bool, Count>&, std::size_t) noexcept -> void {}

    auto consume(std::size_t, std::size_t) noexcept -> CandidateState { std::terminate(); }

    auto validate(std::size_t, std::size_t) noexcept -> void { std::terminate(); }

    template<typename Output>
    auto failure(std::size_t, std::size_t) noexcept -> Output {
        std::terminate();
    }

    template<typename Product, typename Output>
    auto race_success(std::size_t, std::size_t) noexcept -> Output {
        std::terminate();
    }

    template<typename Output>
    auto forward_success(std::size_t, std::size_t) noexcept -> Output {
        std::terminate();
    }

    auto product() noexcept -> carven::runtime::Tuple<> { return {}; }
};

template<typename Input, typename... Rest>
struct CompositionSlots<Input, Rest...> final {
    using Traits = CompositionInput<Input>;
    typename Traits::ChildType child;
    std::optional<typename Traits::CompletionType> completion;
    [[no_unique_address]] typename Traits::ValidationSite validation_site;
    CompositionSlots<Rest...> tail;

    // Member initialization starts each operation in input order. All members
    // are established before the driver dispatches any child startup.
    explicit CompositionSlots(ChildScope& scope, Input& input, Rest&... rest) noexcept
        : child(scope.start(std::move(Traits::operation(input)))),
          completion(std::nullopt),
          validation_site(Traits::site(input)),
          tail(scope, rest...) {}

    template<std::size_t Count>
    auto fill(std::array<ChildSlot*, Count>& children, std::size_t index) noexcept -> void {
        children[index] = &child.slot;
        tail.fill(children, index + 1);
    }

    template<std::size_t Count>
    auto cancel_active(const std::array<bool, Count>& active, std::size_t index) noexcept -> void {
        if (active[index]) {
            cancel(child);
        }
        tail.cancel_active(active, index + 1);
    }

    auto consume(std::size_t selected, std::size_t index) noexcept -> CandidateState {
        if (selected != index) {
            return tail.consume(selected, index + 1);
        }
        require_protocol(!completion.has_value());
        completion.emplace(child.observe().await_resume());
        if (completion->success_if() != nullptr) {
            return CandidateState::Success;
        }
        return completion->is_cancelled() ? CandidateState::Cancelled : CandidateState::Failure;
    }

    auto value() noexcept -> typename Traits::Value {
        require_protocol(completion && completion->success_if() != nullptr);
        if constexpr (std::is_void_v<typename Traits::Value>
                      || std::same_as<typename Traits::Value, carven::runtime::Void>) {
            return {};
        } else {
            return
                typename Traits::Value(carven::runtime::transfer(completion->success_if()->value));
        }
    }

    auto validate(std::size_t selected, std::size_t index) noexcept -> void {
        if (selected != index) {
            tail.validate(selected, index + 1);
            return;
        }
        if constexpr (Traits::validate) {
            carven::runtime::checked_unicode_scalar(
                completion->success_if()->value,
                validation_site
            );
        }
    }

    template<typename Output>
    auto failure(std::size_t selected, std::size_t index) noexcept -> Output {
        if (selected != index) {
            return tail.template failure<Output>(selected, index + 1);
        }
        require_protocol(completion.has_value());
        return forward_nominal<Output>(*completion);
    }

    template<typename Product, typename Output>
    auto race_success(std::size_t selected, std::size_t index) noexcept -> Output {
        if (selected != index) {
            return tail.template race_success<Product, Output>(selected, index + 1);
        }
        return Output::success_from([&]() noexcept -> Product {
            return {.head = selected, .tail = {.head = value(), .tail = {}}};
        });
    }

    template<typename Output>
    auto forward_success(std::size_t selected, std::size_t index) noexcept -> Output {
        if (selected != index) {
            return tail.template forward_success<Output>(selected, index + 1);
        }
        return Output::adopt_success(std::move(*completion));
    }

    auto product() noexcept -> carven::runtime::
        Tuple<typename Traits::Value, typename CompositionInput<Rest>::Value...> {
        return {.head = value(), .tail = tail.product()};
    }
};

} // namespace detail

template<bool Validate, typename Input>
auto composition_operand(Input input, carven::runtime::SourceSite site) noexcept
    -> detail::CompositionOperand<Input, Validate> {
    return {.operation = std::move(input), .site = site};
}

// Private generated-code ABI. Input parameters own cold operations; constructing
// or discarding this coroutine does not start them. Product delivery follows
// the normal value transfer requirements of each completed slot.
template<CompositionPolicy Policy, typename Result, typename... Failures, typename... Inputs>
auto compose(Inputs... inputs) noexcept -> AsyncOperation<Result, Failures...> {
    using Output = Completion<Result, Failures...>;
    constexpr auto count = sizeof...(Inputs);
    if (cancel_requested()) {
        co_return Output::cancelled();
    }
    if constexpr (count == 0) {
        static_assert(Policy == CompositionPolicy::WhenAll && std::is_void_v<Result>);
        co_return Output::success();
    } else {
        auto scope = ChildScope(co_await current_activation());
        auto slots = detail::CompositionSlots<Inputs...>(scope, inputs...);
        auto children = std::array<detail::ChildSlot*, count>();
        slots.fill(children, 0);
        auto active = std::array<bool, count>();
        active.fill(true);
        auto remaining = count;
        // Race records a failure here; other policy instantiations only read it.
        // NOLINTNEXTLINE(misc-const-correctness)
        auto fallback = std::optional<std::size_t>();
        auto output = std::optional<Output>();
        while (remaining != 0) {
            if (cancel_requested()) {
                output.emplace(Output::cancelled());
                break;
            }
            const auto selected = co_await WaitReadyAwaiter<count>(children, &active, true);
            if (selected.head == count || cancel_requested()) {
                output.emplace(Output::cancelled());
                break;
            }
            const auto state = slots.consume(selected.head, 0);
            active[selected.head] = false;
            --remaining;
            if (cancel_requested()) {
                output.emplace(Output::cancelled());
                break;
            }
            if (state == detail::CandidateState::Success) {
                slots.validate(selected.head, 0);
            }
            if constexpr (Policy == CompositionPolicy::WhenAll) {
                if (state == detail::CandidateState::Failure) {
                    output.emplace(slots.template failure<Output>(selected.head, 0));
                    break;
                }
                if (state == detail::CandidateState::Cancelled && !output) {
                    // Cancellation owns no source payload; a later failure can replace it.
                    output.emplace(Output::cancelled());
                    slots.cancel_active(active, 0);
                }
            } else {
                if (state == detail::CandidateState::Success) {
                    if constexpr (Policy == CompositionPolicy::ForwardFirst) {
                        output.emplace(slots.template forward_success<Output>(selected.head, 0));
                    } else {
                        output.emplace(
                            slots.template race_success<Result, Output>(selected.head, 0)
                        );
                    }
                    break;
                }
                if constexpr (Policy == CompositionPolicy::WhenAny
                              || Policy == CompositionPolicy::ForwardFirst) {
                    output.emplace(
                        state == detail::CandidateState::Cancelled
                            ? Output::cancelled()
                            : slots.template failure<Output>(selected.head, 0)
                    );
                    break;
                } else if (state == detail::CandidateState::Failure && !fallback) {
                    fallback = selected.head;
                }
            }
        }
        if (!output) {
            if (cancel_requested()) {
                output.emplace(Output::cancelled());
            } else if constexpr (Policy == CompositionPolicy::WhenAll) {
                output.emplace(Output::success_from([&]() noexcept -> Result {
                    return slots.product();
                }));
            } else {
                output.emplace(
                    fallback ? slots.template failure<Output>(*fallback, 0) : Output::cancelled()
                );
            }
        }
        co_await scope.close(
            remaining != 0 ? ClosingPolicy::RequestCancelAndClose : ClosingPolicy::CloseOnly
        );
        co_return std::move(*output);
    }
}

template<typename Result, typename... Failures, typename... Inputs>
auto when_all(Inputs... inputs) noexcept -> AsyncOperation<Result, Failures...> {
    return compose<CompositionPolicy::WhenAll, Result, Failures...>(std::move(inputs)...);
}

template<typename Result, typename... Failures, typename... Inputs>
auto when_any(Inputs... inputs) noexcept -> AsyncOperation<Result, Failures...> {
    return compose<CompositionPolicy::WhenAny, Result, Failures...>(std::move(inputs)...);
}

template<typename Result, typename... Failures, typename... Inputs>
auto first_successful(Inputs... inputs) noexcept -> AsyncOperation<Result, Failures...> {
    return compose<CompositionPolicy::FirstSuccessful, Result, Failures...>(std::move(inputs)...);
}

} // namespace carven::runtime::execution
