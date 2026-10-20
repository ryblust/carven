#pragma once

#include <carven/runtime/value/storage.hpp>

#include <concepts>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime {

template<typename Result, typename... Failures>
class Outcome;

namespace execution {

template<typename Result, typename... Failures>
class Completion;

} // namespace execution

// Exact type queries shared by outcome construction and callable adaptation.
// References and cv-qualified types do not match Outcome specializations.
template<typename Value>
struct OutcomeTraits final {
    static constexpr auto is_outcome = false;
};

template<typename Value, typename... Failures>
struct OutcomeTraits<Outcome<Value, Failures...>> final {
    static constexpr auto is_outcome = true;
    using Result = Value;
};

namespace detail {

struct Stopped final {};

template<typename Needle, typename... Haystack>
concept ContainsExact = (std::same_as<Needle, Haystack> || ...);

template<typename... Types>
struct UniqueTypes final : std::true_type {};

template<typename First, typename... Rest>
struct UniqueTypes<First, Rest...> final
    : std::bool_constant<(!ContainsExact<First, Rest...>) && UniqueTypes<Rest...>::value> {};

template<typename Source, typename Destination>
struct StrictOutcomeSubset final : std::false_type {};

template<
    typename SourceResult,
    typename... SourceFailures,
    typename DestinationResult,
    typename... DestinationFailures>
struct StrictOutcomeSubset<
    Outcome<SourceResult, SourceFailures...>,
    Outcome<DestinationResult, DestinationFailures...>>
    final : std::bool_constant<
                std::same_as<SourceResult, DestinationResult>
                && (sizeof...(SourceFailures) < sizeof...(DestinationFailures))
                && (ContainsExact<SourceFailures, DestinationFailures...> && ...)> {};

} // namespace detail

// Widening preserves the result type and strictly adds admitted failure types.
// Source and destination are matched without removing references or cv-qualifiers.
template<typename Source, typename Destination>
concept OutcomeWidening = detail::StrictOutcomeSubset<Source, Destination>::value;

template<typename Result, typename... Failures>
class Outcome final {
    static_assert(detail::UniqueTypes<Failures...>::value, "Outcome failure types must be unique");

public:
    using Success = detail::SuccessState<Result>;

private:
    static constexpr auto inline_success = detail::InlineValue<Result>::value;
    using SuccessPayload = detail::Payload<Success, inline_success>;

    template<typename Failure>
    using FailurePayload = detail::Payload<Failure, detail::InlineValue<Failure>::value>;

    struct Empty final {};

    using State = std::variant<SuccessPayload, FailurePayload<Failures>..., detail::Stopped, Empty>;

    template<typename, typename...>
    friend class Outcome;

    template<typename, typename...>
    friend class execution::Completion;

    template<typename>
    friend class SuccessBinding;

    template<typename Factory>
    constexpr explicit Outcome(std::in_place_type_t<Success>, Factory&& factory) noexcept
        : state(
              std::in_place_type<SuccessPayload>,
              std::in_place,
              std::in_place,
              std::forward<Factory>(factory)
          ) {}

    constexpr explicit Outcome(std::in_place_type_t<Success>) noexcept
        : state(std::in_place_type<SuccessPayload>, std::in_place) {}

    constexpr explicit Outcome(detail::Stopped) noexcept
        : state(std::in_place_type<detail::Stopped>) {}

    template<typename Failure, typename Value>
    constexpr explicit Outcome(std::in_place_type_t<Failure>, Value&& value) noexcept
        : state(
              std::in_place_type<FailurePayload<Failure>>,
              std::in_place,
              std::forward<Value>(value)
          ) {}

    template<typename Source>
    static constexpr auto adopt_state(Source& source) noexcept -> State {
        auto result = std::visit(
            []<typename Alternative>(Alternative& alternative) noexcept -> State {
                if constexpr (std::same_as<Alternative, detail::Stopped>) {
                    return State(std::in_place_type<detail::Stopped>);
                } else if constexpr (std::same_as<Alternative, typename Source::Empty>) {
                    std::terminate();
                } else {
                    return State(std::in_place_type<Alternative>, std::move(alternative));
                }
            },
            source.state
        );
        source.state.template emplace<typename Source::Empty>();
        return result;
    }

    constexpr explicit Outcome(SuccessPayload&& payload) noexcept
        : state(std::in_place_type<SuccessPayload>, std::move(payload)) {}

public:
    Outcome() = delete;
    Outcome(const Outcome&) = delete;

    constexpr Outcome(Outcome&&) noexcept = default;

    template<typename SourceResult, typename... SourceFailures>
        requires OutcomeWidening<
            Outcome<SourceResult, SourceFailures...>,
            Outcome<Result, Failures...>>
    constexpr Outcome(Outcome<SourceResult, SourceFailures...>&& source) noexcept
        : state(adopt_state(source)) {}

    constexpr ~Outcome() = default;

    auto operator=(const Outcome&) -> Outcome& = delete;
    auto operator=(Outcome&&) -> Outcome& = delete;

    template<typename Factory>
        requires (!std::is_void_v<Result>) && std::same_as<std::invoke_result_t<Factory>, Result>
    static constexpr auto success_from(Factory&& factory) noexcept -> Outcome {
        return Outcome(std::in_place_type<Success>, std::forward<Factory>(factory));
    }

    static constexpr auto success() noexcept -> Outcome
        requires std::is_void_v<Result>
    {
        return Outcome(std::in_place_type<Success>);
    }

    static constexpr auto stopped() noexcept -> Outcome { return Outcome(detail::Stopped {}); }

    template<typename Factory>
        requires std::same_as<std::invoke_result_t<Factory>, Outcome>
        || OutcomeWidening<std::invoke_result_t<Factory>, Outcome>
        || (!OutcomeTraits<std::remove_cvref_t<std::invoke_result_t<Factory>>>::is_outcome
            && (!std::is_void_v<Result> || std::is_void_v<std::invoke_result_t<Factory>>))
    static constexpr auto invoke(Factory&& factory) noexcept -> Outcome {
        using Source = std::invoke_result_t<Factory>;
        if constexpr (std::same_as<Source, Outcome> || OutcomeWidening<Source, Outcome>) {
            return std::forward<Factory>(factory)();
        } else if constexpr (std::is_void_v<Result>) {
            std::forward<Factory>(factory)();
            return success();
        } else {
            return success_from([&]() noexcept -> Result {
                return std::forward<Factory>(factory)();
            });
        }
    }

    constexpr auto is_stopped() const noexcept -> bool {
        return std::holds_alternative<detail::Stopped>(state);
    }

    // A trivially copied result needs no in-place factory.
    template<typename Value = Result>
        requires std::same_as<Value, Result>
        && (!std::is_void_v<Value>)
        && detail::trivially_copied_and_destroyed<Value>
    static constexpr auto success(std::type_identity_t<Value> value) noexcept -> Outcome {
        return Outcome(std::in_place_type<Success>, [&]() noexcept -> Result { return value; });
    }

    template<typename Value>
        requires detail::ContainsExact<std::remove_cvref_t<Value>, Failures...>
        && std::is_constructible_v<std::remove_cvref_t<Value>, Value&&>
    static constexpr auto failure(Value&& value) noexcept -> Outcome {
        using Failure = std::remove_cvref_t<Value>;
        return Outcome(std::in_place_type<Failure>, std::forward<Value>(value));
    }

    template<typename Source>
        requires std::same_as<typename std::remove_cvref_t<Source>::Success, Success>
        && (!std::is_lvalue_reference_v<Source>)
        && (!std::is_const_v<std::remove_reference_t<Source>>)
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): Consume the payload, then clear the same source carrier.
    static constexpr auto adopt_success(Source&& source) noexcept -> Outcome {
        auto* payload = std::get_if<SuccessPayload>(&source.state);
        if (payload == nullptr) {
            std::terminate();
        }
        auto result = Outcome(std::move(*payload));
        source.state.template emplace<typename std::remove_cvref_t<Source>::Empty>();
        return result;
    }

    constexpr auto success_if() & noexcept -> Success* {
        auto* payload = std::get_if<SuccessPayload>(&state);
        return payload == nullptr ? nullptr : payload->get();
    }

    constexpr auto success_if() const& noexcept -> const Success* {
        const auto* payload = std::get_if<SuccessPayload>(&state);
        return payload == nullptr ? nullptr : payload->get();
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto failure_if() & noexcept -> Failure* {
        auto* payload = std::get_if<FailurePayload<Failure>>(&state);
        return payload == nullptr ? nullptr : payload->get();
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto failure_if() const& noexcept -> const Failure* {
        const auto* payload = std::get_if<FailurePayload<Failure>>(&state);
        return payload == nullptr ? nullptr : payload->get();
    }

private:
    State state;
};

} // namespace carven::runtime
