#pragma once

#include <carven/runtime/call/passing.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime {

template<typename... Elements>
struct Tuple;

namespace detail {

template<typename Result>
struct SuccessState final {
    Result value;

    template<typename Factory>
    constexpr explicit SuccessState(std::in_place_t, Factory&& factory) noexcept
        : value(std::forward<Factory>(factory)()) {}
};

template<>
struct SuccessState<void> final {};

// Generated declarations publish source value semantics before their definitions.
// Native class representations remain independently stored unless explicitly known.
template<typename Value>
struct InlineValue : std::bool_constant<[] {
    if constexpr (requires { carven_inline_value(static_cast<Value*>(nullptr)); }) {
        return decltype(carven_inline_value(static_cast<Value*>(nullptr)))::value;
    } else {
        return std::is_void_v<Value>
            || std::is_arithmetic_v<Value>
            || std::is_pointer_v<Value>
            || std::is_enum_v<Value>;
    }
}()> {};

template<typename Element, std::size_t Size>
struct InlineValue<std::array<Element, Size>> : InlineValue<Element> {};

template<typename... Elements>
struct InlineValue<Tuple<Elements...>> : std::bool_constant<(InlineValue<Elements>::value && ...)> {
};

// Payload owns its value, including indirect allocation and destruction.
template<typename Value, bool Inline>
class Payload final {
    using Storage = std::conditional_t<Inline, Value, Value*>;
    Storage storage;

    template<typename... Arguments>
    static constexpr auto make_storage(Arguments&&... arguments) noexcept -> Storage {
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
    constexpr explicit Payload(std::in_place_t, Arguments&&... arguments) noexcept
        : storage(make_storage(std::forward<Arguments>(arguments)...)) {}

    Payload(const Payload&) = delete;

    constexpr Payload(Payload&& source) noexcept
        : storage([&]() noexcept -> Storage {
              if constexpr (Inline) {
                  return transfer(source.storage);
              } else {
                  return std::exchange(source.storage, nullptr);
              }
          }()) {}

    auto operator=(const Payload&) -> Payload& = delete;
    auto operator=(Payload&&) -> Payload& = delete;

    constexpr ~Payload()
        requires Inline
    = default;

    constexpr ~Payload()
        requires (!Inline)
    {
        delete storage;
    }

    constexpr auto get() noexcept -> Value* {
        if constexpr (Inline) {
            return std::addressof(storage);
        } else {
            return storage;
        }
    }

    constexpr auto get() const noexcept -> const Value* {
        if constexpr (Inline) {
            return std::addressof(storage);
        } else {
            return storage;
        }
    }
};

} // namespace detail

// A fresh result keeps the selected payload when it enters lexical storage.
// Named source values use ordinary read and take operations independently.
template<typename Result>
class SuccessBinding final {
    static_assert(!std::is_void_v<Result>);
    static constexpr auto inline_success = detail::InlineValue<Result>::value;
    using Success = detail::SuccessState<Result>;
    using Storage = detail::Payload<Success, inline_success>;
    Storage storage;

    template<typename Source>
    static auto bind(Source& source) noexcept -> Storage {
        auto* payload = std::get_if<Storage>(&source.state);
        if (payload == nullptr) {
            std::terminate();
        }
        auto result = Storage(std::move(*payload));
        source.state.template emplace<typename Source::Empty>();
        return result;
    }

public:
    template<typename Source>
        requires std::same_as<typename Source::Success, Success> && (!std::is_const_v<Source>)
    explicit SuccessBinding(Source& source) noexcept
        : storage(bind(source)) {}

    template<typename Factory>
        requires std::same_as<std::invoke_result_t<Factory>, Result>
    explicit SuccessBinding(std::in_place_t, Factory&& factory) noexcept
        : storage(std::in_place, std::in_place, std::forward<Factory>(factory)) {}

    SuccessBinding(const SuccessBinding&) = delete;
    SuccessBinding(SuccessBinding&&) = delete;
    auto operator=(const SuccessBinding&) -> SuccessBinding& = delete;
    auto operator=(SuccessBinding&&) -> SuccessBinding& = delete;

    ~SuccessBinding() = default;

    auto get() & noexcept -> Result& { return storage.get()->value; }

    auto get() const& noexcept -> const Result& { return storage.get()->value; }
};

} // namespace carven::runtime
