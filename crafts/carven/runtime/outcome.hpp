#pragma once

#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime {

template<typename Result, typename... Failures>
class Outcome;

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

template<typename Needle, typename... Haystack>
concept ContainsExact = (std::same_as<Needle, Haystack> || ...);

template<typename... Types>
struct UniqueTypes final : std::true_type {};

template<typename First, typename... Rest>
struct UniqueTypes<First, Rest...> final
    : std::bool_constant<(!ContainsExact<First, Rest...>) && UniqueTypes<Rest...>::value> {};

template<typename Result>
struct SuccessState final {
    Result value;
};

template<>
struct SuccessState<void> final {};

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
// This relation does not establish payload constructibility; consumers check it separately.
template<typename Source, typename Destination>
concept OutcomeWidening = detail::StrictOutcomeSubset<Source, Destination>::value;

template<typename Result, typename... Failures>
class Outcome final {
    static_assert(sizeof...(Failures) > 0, "Outcome requires at least one failure type");
    static_assert(detail::UniqueTypes<Failures...>::value, "Outcome failure types must be unique");

    using Success = detail::SuccessState<Result>;
    using State = std::variant<Success, Failures...>;

    template<typename, typename...>
    friend class Outcome;

    template<typename Value>
    constexpr explicit Outcome(std::in_place_type_t<Success>, Value&& value) noexcept
        : state(std::in_place_type<Success>, std::forward<Value>(value)) {}

    constexpr explicit Outcome(std::in_place_type_t<Success>) noexcept
        : state(std::in_place_type<Success>) {}

    template<typename Failure, typename Value>
    constexpr explicit Outcome(std::in_place_type_t<Failure>, Value&& value) noexcept
        : state(std::in_place_type<Failure>, std::forward<Value>(value)) {}

public:
    Outcome() = delete;
    Outcome(const Outcome&) = delete;
    constexpr Outcome(Outcome&&) noexcept = default;

    template<typename SourceResult, typename... SourceFailures>
        requires OutcomeWidening<
                     Outcome<SourceResult, SourceFailures...>,
                     Outcome<Result, Failures...>>
        && std::is_move_constructible_v<Success>
        && (std::is_move_constructible_v<SourceFailures> && ...)
    constexpr Outcome(Outcome<SourceResult, SourceFailures...>&& source) noexcept
        : state(
              std::visit(
                  []<typename Alternative>(Alternative& alternative) noexcept -> State {
                      return State(std::in_place_type<Alternative>, std::move(alternative));
                  },
                  source.state
              )
          ) {}

    constexpr ~Outcome() = default;

    auto operator=(const Outcome&) -> Outcome& = delete;
    auto operator=(Outcome&&) -> Outcome& = delete;

    template<typename Value>
        requires (!std::is_void_v<Result> && std::is_constructible_v<Result, Value &&>)
    static constexpr auto success(Value&& value) noexcept -> Outcome {
        return Outcome(std::in_place_type<Success>, std::forward<Value>(value));
    }

    static constexpr auto success() noexcept -> Outcome
        requires std::is_void_v<Result>
    {
        return Outcome(std::in_place_type<Success>);
    }

    template<typename Value>
        requires detail::ContainsExact<std::remove_cvref_t<Value>, Failures...>
        && std::is_constructible_v<std::remove_cvref_t<Value>, Value&&>
    static constexpr auto failure(Value&& value) noexcept -> Outcome {
        using Failure = std::remove_cvref_t<Value>;
        return Outcome(std::in_place_type<Failure>, std::forward<Value>(value));
    }

    constexpr auto success_if() & noexcept -> Success* { return std::get_if<Success>(&state); }

    constexpr auto success_if() const& noexcept -> const Success* {
        return std::get_if<Success>(&state);
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto failure_if() & noexcept -> Failure* {
        return std::get_if<Failure>(&state);
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto failure_if() const& noexcept -> const Failure* {
        return std::get_if<Failure>(&state);
    }

private:
    State state;
};

} // namespace carven::runtime
