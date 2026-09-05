#pragma once

#include "outcome.hpp"

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
        if constexpr (direct_result<Source, Destination>) {
            return true;
        } else if constexpr (OutcomeTraits<Destination>::is_outcome
                             && !OutcomeTraits<Source>::is_outcome) {
            using DestinationResult = typename OutcomeTraits<Destination>::Result;
            return std::same_as<Source, DestinationResult>;
        }
        return false;
    }

    template<typename Destination, typename Callable, typename... CallArguments>
        requires std::is_invocable_v<Callable, CallArguments...>
        && (result_compatible<std::invoke_result_t<Callable, CallArguments...>, Destination>())
    static auto invoke_adapted(Callable&& callable, CallArguments&&... arguments) noexcept
        -> Destination {
        using Source = std::invoke_result_t<Callable, CallArguments...>;
        if constexpr (direct_result<Source, Destination>) {
            if constexpr (std::is_void_v<Destination>) {
                std::invoke(
                    std::forward<Callable>(callable),
                    std::forward<CallArguments>(arguments)...
                );
            } else {
                return std::invoke(
                    std::forward<Callable>(callable),
                    std::forward<CallArguments>(arguments)...
                );
            }
        } else if constexpr (std::is_void_v<Source>) {
            std::invoke(
                std::forward<Callable>(callable),
                std::forward<CallArguments>(arguments)...
            );
            return Destination::success();
        } else {
            return Destination::success(
                std::invoke(
                    std::forward<Callable>(callable),
                    std::forward<CallArguments>(arguments)...
                )
            );
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

    using Thunk = Result (*)(Entity, Arguments&&...) noexcept;

    template<typename Callable>
    static auto invoke_object(Entity entity, Arguments&&... arguments) noexcept -> Result {
        // NOLINTNEXTLINE(misc-const-correctness): FunctionRef admits stateful callables with non-const operator().
        auto& callable = *static_cast<Callable*>(const_cast<void*>(entity.object));
        return invoke_adapted<Result>(callable, std::forward<Arguments>(arguments)...);
    }

    template<typename SourcePointer>
    static auto invoke_function(Entity entity, Arguments&&... arguments) noexcept -> Result {
        const auto function = reinterpret_cast<SourcePointer>(entity.function);
        return invoke_adapted<Result>(function, std::forward<Arguments>(arguments)...);
    }

public:
    FunctionRef() = delete;
    FunctionRef(std::nullptr_t) = delete;

    // Function targets are stored by value; object targets remain borrowed.
    template<typename SourceResult, bool Noexcept>
        requires (result_compatible<SourceResult, Result>())
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
                     && std::is_invocable_v<Callable&, Arguments...>
                     && (result_compatible<std::invoke_result_t<Callable&, Arguments...>, Result>())
                     && (!std::same_as<std::remove_cv_t<Callable>, FunctionRef>)
    explicit FunctionRef(Callable& callable CARVEN_RUNTIME_LIFETIME_BOUND) noexcept
        : entity(static_cast<const void*>(std::addressof(callable))),
          thunk(&invoke_object<Callable>) {}

    FunctionRef(const FunctionRef&) = default;
    FunctionRef(FunctionRef&&) = default;
    auto operator=(const FunctionRef&) -> FunctionRef& = default;
    auto operator=(FunctionRef&&) -> FunctionRef& = default;

    auto operator()(Arguments... arguments) const noexcept -> Result {
        if constexpr (std::is_void_v<Result>) {
            thunk(entity, std::forward<Arguments>(arguments)...);
        } else {
            return thunk(entity, std::forward<Arguments>(arguments)...);
        }
    }

private:
    Entity entity;
    Thunk thunk;
};

}

#undef CARVEN_RUNTIME_LIFETIME_BOUND
