#pragma once

#include <carven/runtime/value/outcome.hpp>

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime::execution {

// Source value payloads travel with the completion. Other payloads retain
// independent storage across completion and coroutine-frame lifetimes.
template<typename Result, typename... Failures>
class Completion final {
    static_assert(carven::runtime::detail::UniqueTypes<Failures...>::value);

    template<typename T, typename... E>
    friend class Completion;

    template<typename T, typename... E>
    friend class AsyncOperation;

    template<typename T>
    friend class carven::runtime::SuccessBinding;

    template<typename, typename...>
    friend class carven::runtime::Outcome;

    struct Cancelled final {};

    struct Empty final {};

    static constexpr auto inline_success = carven::runtime::detail::InlineValue<Result>::value;

    using SuccessPayload = carven::runtime::detail::
        Payload<carven::runtime::detail::SuccessState<Result>, inline_success>;

    template<typename Failure>
    using FailurePayload = carven::runtime::detail::
        Payload<Failure, carven::runtime::detail::InlineValue<Failure>::value>;

    using Stopped = carven::runtime::detail::Stopped;

    using State =
        std::variant<SuccessPayload, FailurePayload<Failures>..., Cancelled, Stopped, Empty>;

    State state;

    Completion() noexcept
        : state(std::in_place_type<Empty>) {}

    auto empty() const noexcept -> bool { return std::holds_alternative<Empty>(state); }

    // The promise selects one outcome into its initially empty carrier.
    auto initialize(Completion&& source) noexcept -> void {
        std::destroy_at(std::addressof(state));
        std::construct_at(std::addressof(state), std::move(source.state));
        source.state.template emplace<Empty>();
    }

    template<typename Alternative>
    using Payload = std::conditional_t<
        std::same_as<Alternative, carven::runtime::detail::SuccessState<Result>>,
        SuccessPayload,
        FailurePayload<Alternative>>;

    template<typename Alternative, typename... Arguments>
    explicit Completion(std::in_place_type_t<Alternative>, Arguments&&... arguments) noexcept
        : state(
              std::in_place_type<Payload<Alternative>>,
              std::in_place,
              std::forward<Arguments>(arguments)...
          ) {}

    explicit Completion(SuccessPayload&& payload) noexcept
        : state(std::in_place_type<SuccessPayload>, std::move(payload)) {}

    explicit Completion(Cancelled) noexcept
        : state(std::in_place_type<Cancelled>) {}

    explicit Completion(Stopped) noexcept
        : state(std::in_place_type<Stopped>) {}

public:
    using Success = carven::runtime::detail::SuccessState<Result>;
    Completion(const Completion&) = delete;

    Completion(Completion&& other) noexcept
        : state(std::move(other.state)) {
        other.state.template emplace<Empty>();
    }

    ~Completion() = default;

    auto operator=(const Completion&) -> Completion& = delete;
    auto operator=(Completion&&) -> Completion& = delete;

    template<typename Factory>
    static auto success_from(Factory&& factory) noexcept -> Completion
        requires (!std::is_void_v<Result> && std::same_as<std::invoke_result_t<Factory>, Result>)
    {
        return Completion(
            std::in_place_type<Success>,
            std::in_place,
            std::forward<Factory>(factory)
        );
    }

    static auto success() noexcept -> Completion
        requires std::is_void_v<Result>
    {
        return Completion(std::in_place_type<Success>);
    }

    template<typename Value = Result>
    static auto success(std::type_identity_t<Value> value) noexcept -> Completion
        requires (
            std::same_as<Value, Result>
            && !std::is_void_v<Value>
            && carven::runtime::detail::trivially_copied_and_destroyed<Value>
        )
    {
        return success_from([&]() noexcept -> Value { return value; });
    }

    template<typename Failure>
    static auto failure(Failure&& value) noexcept -> Completion
        requires carven::runtime::detail::ContainsExact<std::remove_cvref_t<Failure>, Failures...>
        && std::is_constructible_v<std::remove_cvref_t<Failure>, Failure&&>
    {
        return Completion(
            std::in_place_type<std::remove_cvref_t<Failure>>,
            std::forward<Failure>(value)
        );
    }

    static auto cancelled() noexcept -> Completion { return Completion(Cancelled {}); }

    static auto stopped() noexcept -> Completion { return Completion(Stopped {}); }

    auto is_stopped() const noexcept -> bool { return std::holds_alternative<Stopped>(state); }

    // Private generated-code ABI: success-only ownership transfer across exact
    // Result types. Source-value construction is separate from carrier delivery.
    template<typename Source>
        requires std::same_as<typename std::remove_cvref_t<Source>::Success, Success>
        && (!std::is_lvalue_reference_v<Source>)
        && (!std::is_const_v<std::remove_reference_t<Source>>)
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): Consume the payload, then clear the same source carrier.
    static auto adopt_success(Source&& source) noexcept -> Completion {
        auto* payload = std::get_if<SuccessPayload>(&source.state);
        if (payload == nullptr) {
            std::terminate();
        }
        auto result = Completion(std::move(*payload));
        source.state.template emplace<typename std::remove_cvref_t<Source>::Empty>();
        return result;
    }

    auto success_if() & noexcept -> Success* {
        auto* value = std::get_if<SuccessPayload>(&state);
        return value == nullptr ? nullptr : value->get();
    }

    auto success_if() const& noexcept -> const Success* {
        const auto* value = std::get_if<SuccessPayload>(&state);
        return value == nullptr ? nullptr : value->get();
    }

    template<typename Failure>
    auto failure_if() & noexcept -> Failure*
        requires carven::runtime::detail::ContainsExact<Failure, Failures...>
    {
        auto* value = std::get_if<FailurePayload<Failure>>(&state);
        return value == nullptr ? nullptr : value->get();
    }

    template<typename Failure>
    auto failure_if() const& noexcept -> const Failure*
        requires carven::runtime::detail::ContainsExact<Failure, Failures...>
    {
        const auto* value = std::get_if<FailurePayload<Failure>>(&state);
        return value == nullptr ? nullptr : value->get();
    }

    auto is_cancelled() const noexcept -> bool { return std::holds_alternative<Cancelled>(state); }
};

} // namespace carven::runtime::execution
