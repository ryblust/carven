#pragma once

#include "outcome.hpp"
#include "passing.hpp"

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::lifetimebound)
#define CARVEN_RUNTIME_LIFETIME_BOUND [[clang::lifetimebound]]
#endif
#endif

#ifndef CARVEN_RUNTIME_LIFETIME_BOUND
#define CARVEN_RUNTIME_LIFETIME_BOUND
#endif

namespace carven::runtime {

template<typename Signature>
class FunctionRef;

template<typename Result, typename... Arguments>
class FunctionRef<Result(Arguments...) noexcept> final {
private:
    template<typename Source, typename Destination>
    static constexpr auto direct_result =
        std::same_as<Source, Destination> || OutcomeWidening<Source, Destination>;

    template<typename Source, typename Destination>
    static consteval auto result_compatible() noexcept -> bool {
        if constexpr (std::same_as<Source, Destination>) {
            return true;
        } else if constexpr (OutcomeWidening<Source, Destination>) {
            return std::is_constructible_v<Destination, Source&&>;
        } else if constexpr (OutcomeTraits<Destination>::is_outcome
                             && !OutcomeTraits<Source>::is_outcome) {
            using DestinationResult = typename OutcomeTraits<Destination>::Result;
            return std::same_as<Source, DestinationResult>;
        }
        return false;
    }

    template<typename Callable>
    static constexpr auto invocable = requires (Callable&& callable, Arguments&... arguments) {
        std::invoke(std::forward<Callable>(callable), deliver_argument<Arguments>(arguments)...);
    };

    template<typename Callable>
        requires invocable<Callable>
    using InvocationResult = std::invoke_result_t<
        Callable,
        decltype(deliver_argument<Arguments>(std::declval<Arguments&>()))...>;

    template<typename Callable>
    static consteval auto compatible() noexcept -> bool {
        if constexpr (invocable<Callable>) {
            return result_compatible<InvocationResult<Callable>, Result>();
        }
        return false;
    }

    template<typename Callable>
        requires (compatible<Callable>())
    static auto invoke_adapted(Callable&& callable, Arguments&... arguments) noexcept -> Result {
        const auto invoke = [&]() noexcept -> InvocationResult<Callable> {
            return std::invoke(
                std::forward<Callable>(callable),
                deliver_argument<Arguments>(arguments)...
            );
        };
        using Source = InvocationResult<Callable>;
        if constexpr (direct_result<Source, Result>) {
            return invoke();
        } else if constexpr (std::is_void_v<Source>) {
            invoke();
            return Result::success();
        } else {
            return Result::success_from(invoke);
        }
    }

    using ErasedFunctionPointer = void (*)();

    union Entity final {
        const void* object;
        ErasedFunctionPointer function;

        constexpr explicit Entity(const void* value) noexcept
            : object(value) {}

        constexpr explicit Entity(ErasedFunctionPointer value) noexcept
            : function(value) {}
    };

    using Thunk = Result (*)(Entity, Arguments&...) noexcept;

    template<typename Callable>
    static auto invoke_object(Entity entity, Arguments&... arguments) noexcept -> Result {
        // NOLINTNEXTLINE(misc-const-correctness): FunctionRef admits stateful callables with non-const operator().
        auto& callable = *static_cast<Callable*>(const_cast<void*>(entity.object));
        return invoke_adapted(callable, arguments...);
    }

    template<typename SourcePointer>
    static auto invoke_function(Entity entity, Arguments&... arguments) noexcept -> Result {
        const auto function = reinterpret_cast<SourcePointer>(entity.function);
        return invoke_adapted(function, arguments...);
    }

    template<typename Callable>
    static auto invoke_stateless(Entity, Arguments&... arguments) noexcept -> Result {
        const auto callable = Callable {};
        return invoke_adapted(callable, arguments...);
    }

public:
    FunctionRef() = delete;
    FunctionRef(std::nullptr_t) = delete;

    // Function targets are stored by value; object targets remain borrowed.
    template<typename SourceResult, bool Noexcept>
        requires (compatible<SourceResult (*)(Arguments...) noexcept(Noexcept)>())
    FunctionRef(SourceResult (*function)(Arguments...) noexcept(Noexcept)) noexcept
        : entity(reinterpret_cast<ErasedFunctionPointer>(function)),
          thunk(&invoke_function<decltype(function)>) {
        if (function == nullptr) {
            std::abort();
        }
    }

    template<typename Callable>
        requires (!std::is_lvalue_reference_v<Callable>)
        && (!std::is_pointer_v<std::remove_cvref_t<Callable>>)
        && requires (Callable&& callable) { FunctionRef(+std::forward<Callable>(callable)); }
    FunctionRef(Callable&& callable) noexcept
        : FunctionRef(+std::forward<Callable>(callable)) {}

    template<typename Callable>
        requires std::is_object_v<Callable>
                     && (!std::is_pointer_v<std::remove_cv_t<Callable>>)
                     && (!std::is_member_pointer_v<std::remove_cv_t<Callable>>)
                     && (!std::is_volatile_v<Callable>)
                     && (compatible<Callable&>())
                     && (!std::same_as<std::remove_cv_t<Callable>, FunctionRef>)
    explicit FunctionRef(Callable& callable CARVEN_RUNTIME_LIFETIME_BOUND) noexcept
        : entity(static_cast<const void*>(std::addressof(callable))),
          thunk(&invoke_object<Callable>) {}

    template<typename Callable>
        requires std::is_empty_v<Callable>
        && std::is_trivially_default_constructible_v<Callable>
        && std::is_trivially_destructible_v<Callable>
        && (compatible<const Callable&>())
    static auto from_stateless(const Callable&) noexcept -> FunctionRef {
        return FunctionRef(Entity(static_cast<const void*>(nullptr)), &invoke_stateless<Callable>);
    }

    FunctionRef(const FunctionRef&) = default;
    FunctionRef(FunctionRef&&) = default;
    auto operator=(const FunctionRef&) -> FunctionRef& = default;
    auto operator=(FunctionRef&&) -> FunctionRef& = default;

    auto operator()(Arguments... arguments) const noexcept -> Result {
        return thunk(entity, arguments...);
    }

private:
    FunctionRef(Entity target, Thunk invocation) noexcept
        : entity(target),
          thunk(invocation) {}

    Entity entity;
    Thunk thunk;
};

} // namespace carven::runtime

#undef CARVEN_RUNTIME_LIFETIME_BOUND
