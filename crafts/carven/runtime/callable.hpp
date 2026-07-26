#pragma once

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <functional>
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

namespace detail {

template<typename Callable, typename Result, typename... Arguments>
concept FunctionRefCallable = std::is_object_v<std::remove_reference_t<Callable>>
    && !std::is_pointer_v<std::remove_cvref_t<Callable>>
    && !std::is_member_pointer_v<std::remove_cvref_t<Callable>>
    && !std::is_volatile_v<std::remove_reference_t<Callable>>
    && std::is_nothrow_invocable_r_v<Result, Callable&, Arguments...>;

} // namespace detail

template<typename Result, typename... Arguments>
class FunctionRef<Result(Arguments...) noexcept> final {
private:
    using ErasedFunctionPointer = void (*)() noexcept;

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
        if constexpr (std::is_void_v<Result>) {
            std::invoke(callable, std::forward<Arguments>(arguments)...);
        } else {
            return std::invoke(callable, std::forward<Arguments>(arguments)...);
        }
    }

    template<typename SourceResult>
    static auto invoke_function(Entity entity, Arguments&&... arguments) noexcept -> Result {
        using SourcePointer = SourceResult (*)(Arguments...) noexcept;
        const auto function = reinterpret_cast<SourcePointer>(entity.function);
        if constexpr (std::is_void_v<Result>) {
            function(std::forward<Arguments>(arguments)...);
        } else {
            return function(std::forward<Arguments>(arguments)...);
        }
    }

public:
    FunctionRef() = delete;
    FunctionRef(std::nullptr_t) = delete;

    // Function targets are stored by value; object targets remain borrowed.
    template<typename SourceResult>
        requires std::is_nothrow_invocable_r_v<
                     Result,
                     SourceResult (*)(Arguments...) noexcept,
                     Arguments...>
    FunctionRef(SourceResult (*function)(Arguments...) noexcept) noexcept
        : entity(reinterpret_cast<ErasedFunctionPointer>(function)),
          thunk(&invoke_function<SourceResult>) {
        if (function == nullptr) {
            std::abort();
        }
    }

    template<typename Callable>
        requires (!std::is_lvalue_reference_v<Callable>)
        && (!std::is_pointer_v<std::remove_cvref_t<Callable>>)
        && requires (Callable&& callable) {
               FunctionRef(+std::forward<Callable>(callable));
               requires noexcept(+std::forward<Callable>(callable));
           }
    FunctionRef(Callable&& callable) noexcept
        : FunctionRef(+std::forward<Callable>(callable)) {}

    template<typename Callable>
        requires detail::FunctionRefCallable<Callable, Result, Arguments...>
                     && (!std::same_as<std::remove_cvref_t<Callable>, FunctionRef>)
    explicit FunctionRef(Callable& callable CARVEN_RUNTIME_LIFETIME_BOUND) noexcept
        : entity(static_cast<const void*>(std::addressof(callable))),
          thunk(&invoke_object<Callable>) {}

    FunctionRef(const FunctionRef&) noexcept = default;
    FunctionRef(FunctionRef&&) noexcept = default;
    auto operator=(const FunctionRef&) noexcept -> FunctionRef& = default;
    auto operator=(FunctionRef&&) noexcept -> FunctionRef& = default;

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
