#pragma once

#include <concepts>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime {

template<typename Result, typename... Failures>
class Outcome;

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

template<typename Result, typename... Failures>
class Outcome final {
    static_assert(sizeof...(Failures) > 0, "Outcome requires at least one failure type");
    static_assert(detail::UniqueTypes<Failures...>::value, "Outcome failure types must be unique");
    static_assert(
        std::is_void_v<Result> || std::is_nothrow_move_constructible_v<Result>,
        "Outcome result must be nothrow move constructible"
    );
    static_assert(
        (std::is_nothrow_move_constructible_v<Failures> && ...),
        "Outcome failures must be nothrow move constructible"
    );

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

    constexpr auto require_success() && noexcept -> Success&& {
        if (!has_value()) {
            std::abort();
        }
        return std::move(std::get<Success>(state));
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto require_failure() && noexcept -> Failure&& {
        auto* value = std::get_if<Failure>(&state);
        if (value == nullptr) {
            std::abort();
        }
        return std::move(*value);
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto require_failure() const& noexcept -> const Failure& {
        const auto* value = std::get_if<Failure>(&state);
        if (value == nullptr) {
            std::abort();
        }
        return *value;
    }

public:
    Outcome() = delete;
    Outcome(const Outcome&) = delete;
    constexpr Outcome(Outcome&&) = default;

    template<typename SourceResult, typename... SourceFailures>
        requires detail::StrictOutcomeSubset<
            Outcome<SourceResult, SourceFailures...>,
            Outcome<Result, Failures...>>::value
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
    constexpr auto operator=(Outcome&& source) noexcept -> Outcome& {
        if (this == &source) {
            return *this;
        }
        std::visit(
            [this]<typename Alternative>(Alternative& alternative) noexcept {
                state.template emplace<Alternative>(std::move(alternative));
            },
            source.state
        );
        return *this;
    }

    template<typename Value>
        requires (!std::is_void_v<Result> && std::is_nothrow_constructible_v<Result, Value &&>)
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
        && std::constructible_from<std::remove_cvref_t<Value>, Value&&>
        && std::is_nothrow_constructible_v<std::remove_cvref_t<Value>, Value&&>
    static constexpr auto failure(Value&& value) noexcept -> Outcome {
        using Failure = std::remove_cvref_t<Value>;
        return Outcome(std::in_place_type<Failure>, std::forward<Value>(value));
    }

    constexpr auto has_value() const noexcept -> bool {
        return std::holds_alternative<Success>(state);
    }

    constexpr auto take_value() && noexcept -> Result
        requires (!std::is_void_v<Result> && std::is_nothrow_move_constructible_v<Result>)
    {
        return std::move(*this).require_success().value;
    }

    constexpr auto take_value() && noexcept -> void
        requires std::is_void_v<Result>
    {
        static_cast<void>(std::move(*this).require_success());
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto holds_failure() const noexcept -> bool {
        return std::holds_alternative<Failure>(state);
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
    constexpr auto failure() const& noexcept -> const Failure& {
        return require_failure<Failure>();
    }

    template<typename Failure>
        requires detail::ContainsExact<Failure, Failures...>
        && std::is_nothrow_move_constructible_v<Failure>
    constexpr auto take_failure() && noexcept -> Failure {
        return std::move(*this).template require_failure<Failure>();
    }

private:
    State state;
};

} // namespace carven::runtime
