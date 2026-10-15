#pragma once

#include <carven/runtime/execution/storage.hpp>
#include <carven/runtime/value/outcome.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime::execution {

namespace detail {

// Its concrete type depends only on Result, never on a failure set.
template<typename Result>
using SuccessState = carven::runtime::detail::SuccessState<Result>;

// Completion owns indirect allocations; Empty records their ownership transfer.
template<typename Value, bool Inline>
class Payload final {
    using Storage = std::conditional_t<Inline, Value, Value*>;
    Storage storage;

    template<typename... Arguments>
    static auto make_storage(Arguments&&... arguments) noexcept -> Storage {
        if constexpr (Inline) {
            return Value(std::forward<Arguments>(arguments)...);
        } else {
            auto* result = new (std::nothrow) Value(std::forward<Arguments>(arguments)...);
            if (result == nullptr) {
                std::terminate();
            }
            return result;
        }
    }

public:
    template<typename... Arguments>
    explicit Payload(std::in_place_t, Arguments&&... arguments) noexcept
        : storage(make_storage(std::forward<Arguments>(arguments)...)) {}

    Payload(const Payload&) = delete;

    Payload(Payload&&) noexcept = default;

    auto operator=(const Payload&) -> Payload& = delete;
    auto operator=(Payload&&) -> Payload& = delete;
    ~Payload() = default;

    auto get() noexcept -> Value* {
        if constexpr (Inline) {
            return std::addressof(storage);
        } else {
            return storage;
        }
    }

    auto get() const noexcept -> const Value* {
        if constexpr (Inline) {
            return std::addressof(storage);
        } else {
            return storage;
        }
    }

    auto release() noexcept -> std::unique_ptr<Value>
        requires (!Inline)
    {
        return std::unique_ptr<Value>(storage);
    }

    auto destroy() noexcept -> void {
        if constexpr (!Inline) {
            delete storage;
        }
    }
};

} // namespace detail

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
    friend class SuccessBinding;

    struct Cancelled final {};

    struct Empty final {};

    static constexpr auto inline_success = detail::InlineValue<Result>::value;

    using SuccessPayload = detail::Payload<detail::SuccessState<Result>, inline_success>;
    template<typename Failure>
    using FailurePayload = detail::Payload<Failure, detail::InlineValue<Failure>::value>;
    using State = std::variant<SuccessPayload, FailurePayload<Failures>..., Cancelled, Empty>;
    State state;

    auto release_success() noexcept -> std::unique_ptr<detail::SuccessState<Result>>
        requires (!inline_success)
    {
        auto* payload = std::get_if<SuccessPayload>(&state);
        if (payload == nullptr) {
            std::terminate();
        }
        auto result = payload->release();
        state.template emplace<Empty>();
        return result;
    }

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
        std::same_as<Alternative, detail::SuccessState<Result>>,
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

public:
    using Success = detail::SuccessState<Result>;
    Completion(const Completion&) = delete;

    Completion(Completion&& other) noexcept
        : state(std::move(other.state)) {
        other.state.template emplace<Empty>();
    }

    ~Completion() noexcept {
        if constexpr (!(inline_success && (detail::InlineValue<Failures>::value && ...))) {
            std::visit(
                [](auto& alternative) noexcept {
                    using Alternative = std::remove_cvref_t<decltype(alternative)>;
                    if constexpr (!std::same_as<Alternative, Cancelled>
                                  && !std::same_as<Alternative, Empty>) {
                        alternative.destroy();
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

    // Private generated-code ABI: success-only ownership transfer across exact
    // Result types. Nominal failure propagation still performs source transfer.
    template<typename... SourceFailures>
    static auto adopt_success(Completion<Result, SourceFailures...>&& source) noexcept
        -> Completion {
        auto* payload = std::get_if<SuccessPayload>(&source.state);
        if (payload == nullptr) {
            std::terminate();
        }
        auto result = Completion(std::move(*payload));
        source.state.template emplace<typename Completion<Result, SourceFailures...>::Empty>();
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

// Successful lexical storage uses native transfer when available; an immovable
// result keeps the independent payload selected by the completed operation.
template<typename Result>
class SuccessBinding final {
    static_assert(!std::is_void_v<Result>);
    static constexpr auto transferable = carven::runtime::detail::TransferConstructible<Result>;
    using Success = detail::SuccessState<Result>;
    using Storage = std::conditional_t<transferable, Success, std::unique_ptr<Success>>;
    Storage storage;

    template<typename... Failures>
    static auto bind(Completion<Result, Failures...>& source) noexcept -> Storage {
        auto* success = source.success_if();
        if (success == nullptr) {
            std::terminate();
        }
        if constexpr (transferable) {
            return Success(std::in_place, [&]() noexcept -> Result {
                return Result(carven::runtime::transfer(success->value));
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

    ~SuccessBinding() = default;

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

} // namespace carven::runtime::execution
