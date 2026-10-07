#pragma once

#include "../outcome.hpp"
#include <exception>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime::async {

namespace detail {
// Its concrete type depends only on Result, never on a failure set.
template<typename Result>
using SuccessState = carven::runtime::detail::SuccessState<Result>;
} // namespace detail

// Primitive success is a value; class and failure payloads have independent
// storage that remains valid after the producing coroutine frame is destroyed.
template<typename Result, typename... Failures>
class Completion final {
    static_assert(carven::runtime::detail::UniqueTypes<Failures...>::value);
    template<typename T, typename... E>
    friend class Completion;
    template<typename T, typename... E>
    friend class Operation;
    template<typename T>
    friend class SuccessBinding;

    struct Cancelled final {};

    struct Empty final {};

    static constexpr auto inline_success =
        std::is_void_v<Result> || std::is_arithmetic_v<Result> || std::is_enum_v<Result>;

    auto release_success() noexcept -> detail::SuccessState<Result>*
        requires (!inline_success)
    {
        auto* result = success_if();
        if (result == nullptr) {
            std::terminate();
        }
        state.template emplace<Empty>();
        return result;
    }

    using SuccessStorage = std::
        conditional_t<inline_success, detail::SuccessState<Result>, detail::SuccessState<Result>*>;
    using State = std::variant<SuccessStorage, Failures*..., Cancelled, Empty>;
    State state;

    Completion() noexcept
        : state(std::in_place_type<Empty>) {}

    auto empty() const noexcept -> bool { return std::holds_alternative<Empty>(state); }

    // The promise selects one outcome into its initially empty carrier.
    auto initialize(Completion&& source) noexcept -> void {
        state = std::move(source.state);
        source.state.template emplace<Empty>();
    }

    template<typename Alternative, typename... Arguments>
    static auto make_state(Arguments&&... arguments) noexcept -> State {
        if constexpr (std::same_as<Alternative, detail::SuccessState<Result>> && inline_success) {
            return State(std::in_place_type<Alternative>, std::forward<Arguments>(arguments)...);
        } else {
            auto* payload = new (std::nothrow) Alternative(std::forward<Arguments>(arguments)...);
            if (payload == nullptr) {
                std::terminate();
            }
            return State(std::in_place_type<Alternative*>, payload);
        }
    }

    template<typename Alternative, typename... Arguments>
    explicit Completion(std::in_place_type_t<Alternative>, Arguments&&... arguments) noexcept
        : state(make_state<Alternative>(std::forward<Arguments>(arguments)...)) {}

    explicit Completion(State value) noexcept
        : state(std::move(value)) {}

public:
    using Success = detail::SuccessState<Result>;
    Completion(const Completion&) = delete;

    Completion(Completion&& other) noexcept
        : state(other.state) {
        other.state.template emplace<Empty>();
    }

    ~Completion() noexcept {
        if constexpr (!inline_success || sizeof...(Failures) != 0) {
            std::visit(
                [](auto alternative) noexcept {
                    if constexpr (std::is_pointer_v<decltype(alternative)>) {
                        delete alternative;
                    }
                },
                state
            );
        }
    }

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
        return Completion(State(std::in_place_type<Success>));
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

    static auto cancelled() noexcept -> Completion {
        return Completion(State(std::in_place_type<Cancelled>));
    }

    // Private generated-code ABI: success-only ownership transfer across exact
    // Result types. Nominal failure propagation still performs source transfer.
    template<typename... SourceFailures>
    static auto adopt_success(Completion<Result, SourceFailures...>&& source) noexcept
        -> Completion {
        auto* success = source.success_if();
        if (success == nullptr) {
            std::terminate();
        }
        if constexpr (std::is_void_v<Result>) {
            source.state.template emplace<typename Completion<Result, SourceFailures...>::Empty>();
            return Completion(State(std::in_place_type<Success>));
        } else if constexpr (inline_success) {
            const auto value = success->value;
            source.state.template emplace<typename Completion<Result, SourceFailures...>::Empty>();
            return Completion::success(value);
        } else {
            source.state.template emplace<typename Completion<Result, SourceFailures...>::Empty>();
            return Completion(State(std::in_place_type<Success*>, success));
        }
    }

    auto success_if() & noexcept -> Success* {
        if constexpr (inline_success) {
            return std::get_if<Success>(&state);
        } else {
            auto* value = std::get_if<Success*>(&state);
            return value == nullptr ? nullptr : *value;
        }
    }

    auto success_if() const& noexcept -> const Success* {
        if constexpr (inline_success) {
            return std::get_if<Success>(&state);
        } else {
            const auto* value = std::get_if<Success*>(&state);
            return value == nullptr ? nullptr : *value;
        }
    }

    template<typename Failure>
    auto failure_if() & noexcept -> Failure*
        requires carven::runtime::detail::ContainsExact<Failure, Failures...>
    {
        auto* value = std::get_if<Failure*>(&state);
        return value == nullptr ? nullptr : *value;
    }

    template<typename Failure>
    auto failure_if() const& noexcept -> const Failure*
        requires carven::runtime::detail::ContainsExact<Failure, Failures...>
    {
        const auto* value = std::get_if<Failure*>(&state);
        return value == nullptr ? nullptr : *value;
    }

    auto is_cancelled() const noexcept -> bool { return std::holds_alternative<Cancelled>(state); }
};

// Successful lexical storage uses native transfer when available; an immovable
// result keeps the independent payload selected by the completed operation.
template<typename Result>
class SuccessBinding final {
    static_assert(!std::is_void_v<Result>);
    static constexpr auto transferable = carven::runtime::detail::TransferConstructible<Result>;
    using Success = detail::SuccessState<Result>;
    using Storage = std::conditional_t<transferable, Success, Success*>;
    Storage storage;

    template<typename... Failures>
    static auto bind(Completion<Result, Failures...>& source) noexcept -> Storage {
        if (source.success_if() == nullptr) {
            std::terminate();
        }
        if constexpr (transferable) {
            return Success(std::in_place, [&]() noexcept -> Result {
                return Result(carven::runtime::transfer(source.success_if()->value));
            });
        } else {
            return source.release_success();
        }
    }

public:
    template<typename... Failures>
    explicit SuccessBinding(Completion<Result, Failures...>& source) noexcept
        : storage(bind(source)) {}

    SuccessBinding(const SuccessBinding&) = delete;
    SuccessBinding(SuccessBinding&&) = delete;
    auto operator=(const SuccessBinding&) -> SuccessBinding& = delete;
    auto operator=(SuccessBinding&&) -> SuccessBinding& = delete;

    ~SuccessBinding() noexcept {
        if constexpr (!transferable) {
            delete storage;
        }
    }

    auto get() & noexcept -> Result& {
        if constexpr (transferable) {
            return storage.value;
        } else {
            return storage->value;
        }
    }

    auto get() const& noexcept -> const Result& {
        if constexpr (transferable) {
            return storage.value;
        } else {
            return storage->value;
        }
    }
};

} // namespace carven::runtime::async
