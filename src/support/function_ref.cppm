module carven:support.function_ref;

import :support.invariant;
import std;

template<typename Signature>
class FunctionRef;

// Function pointers are copied; callable objects are borrowed and must outlive
// every invocation. Empty views are optional recipients, not callable targets.
// Escaping exceptions terminate at the compiler's noexcept callback boundary.
template<typename Result, typename... Arguments>
class FunctionRef<Result(Arguments...) noexcept> final {
private:
    template<typename Callable>
    static consteval auto compatible() noexcept -> bool {
        if constexpr (!std::is_invocable_r_v<Result, Callable, Arguments...>) {
            return false;
        } else {
            return !std::reference_converts_from_temporary_v<
                Result,
                std::invoke_result_t<Callable, Arguments...>>;
        }
    }

    using ErasedFunction = void (*)();

    union Target final {
        const void* object;
        ErasedFunction function;

        constexpr explicit Target(const void* value) noexcept
            : object(value) {}

        explicit Target(ErasedFunction value) noexcept
            : function(value) {}
    };

    using Thunk = Result (*)(Target, Arguments&&...) noexcept;

    template<typename Callable>
    static auto invoke_object(Target target, Arguments&&... arguments) noexcept -> Result {
        // NOLINTNEXTLINE(misc-const-correctness): Borrowed callables may have a non-const operator().
        auto& callable = *static_cast<Callable*>(const_cast<void*>(target.object));
        return std::invoke_r<Result>(callable, std::forward<Arguments>(arguments)...);
    }

    template<typename Pointer>
    static auto invoke_function(Target target, Arguments&&... arguments) noexcept -> Result {
        const auto function = reinterpret_cast<Pointer>(target.function);
        return std::invoke_r<Result>(function, std::forward<Arguments>(arguments)...);
    }

public:
    FunctionRef() noexcept = default;

    // Callback parameter conversion borrows objects without an owning wrapper.
    template<typename Pointer>
        requires std::is_pointer_v<Pointer>
                     && std::is_function_v<std::remove_pointer_t<Pointer>>
                     && (compatible<Pointer>())
    FunctionRef(Pointer function) noexcept
        : target(reinterpret_cast<ErasedFunction>(function)),
          thunk(function == nullptr ? nullptr : &invoke_function<Pointer>) {}

    template<typename Callable>
        requires std::is_lvalue_reference_v<Callable>
                     && std::is_object_v<std::remove_reference_t<Callable>>
                     && (!std::is_pointer_v<std::remove_reference_t<Callable>>)
                     && (!std::is_volatile_v<std::remove_reference_t<Callable>>)
                     && (!std::same_as<std::remove_cvref_t<Callable>, FunctionRef>)
                     && (compatible<Callable>())
    FunctionRef(Callable&& callable [[clang::lifetimebound]]) noexcept
        : target(static_cast<const void*>(std::addressof(std::forward<Callable>(callable)))),
          thunk(&invoke_object<std::remove_reference_t<Callable>>) {}

    // Temporary noncapturing lambdas have a function target with static lifetime.
    template<typename Callable>
        requires (!std::is_lvalue_reference_v<Callable>)
        && (!std::is_pointer_v<std::remove_cvref_t<Callable>>)
        && requires (Callable&& callable) {
               requires std::is_function_v<
                   std::remove_pointer_t<decltype(+std::forward<Callable>(callable))>>;
               FunctionRef(+std::forward<Callable>(callable));
           }
    FunctionRef(Callable&& callable) noexcept
        : FunctionRef(+std::forward<Callable>(callable)) {}

    explicit operator bool() const noexcept { return thunk != nullptr; }

    auto operator()(Arguments... arguments) const noexcept -> Result {
        if (thunk == nullptr) {
            invariant_violation("invoking an empty function view");
        }
        return thunk(target, std::forward<Arguments>(arguments)...);
    }

private:
    Target target {static_cast<const void*>(nullptr)};
    Thunk thunk = nullptr;
};
