#pragma once

#include <cstdint>
#include <memory>
#include <type_traits>

namespace pointer_probe {
inline std::int32_t first = 10;
inline std::int32_t second = 20;

inline auto get() noexcept -> std::int32_t* {
    return &first;
}

inline auto other() noexcept -> std::int32_t* {
    return &second;
}

template<typename T>
auto address(T& value) noexcept -> T* {
    return std::addressof(value);
}

inline auto overload(const std::int32_t*) noexcept -> std::int32_t {
    return 1;
}

inline auto overload(std::int32_t*) noexcept -> std::int32_t {
    return 2;
}

inline auto overload(std::nullptr_t) noexcept -> std::int32_t {
    return 3;
}

template<typename T>
auto deduces_writable_pointer(T) noexcept -> bool {
    return std::is_same_v<T, std::int32_t*>;
}

template<typename T>
auto observes_const_lvalue(T&&) noexcept -> bool {
    return std::is_lvalue_reference_v<T> && std::is_const_v<std::remove_reference_t<T>>;
}

inline auto snapshot(std::int32_t* const& saved, std::int32_t*& slot) noexcept -> bool {
    const auto* original = saved;
    slot = &second;
    return saved == original;
}

inline auto pair(const std::int32_t* const& left, const std::int32_t* right) noexcept -> bool {
    return left == right;
}

inline auto increment(std::int32_t& value, std::int32_t* const& next, std::int32_t*& slot) noexcept
    -> void {
    slot = next;
    value += 1;
}

struct Fixed final {
    std::int32_t value = 91;
    Fixed() = default;
    Fixed(const Fixed&) = delete;

    auto read() const noexcept -> std::int32_t { return value; }
};

inline Fixed fixed;

inline auto fixed_object() noexcept -> Fixed* {
    return &fixed;
}

inline auto readonly_fixed() noexcept -> const Fixed* {
    return &fixed;
}

inline auto output(std::int32_t** slot) noexcept -> void {
    *slot = &first;
}
struct Incomplete;

inline auto opaque() noexcept -> Incomplete* {
    return nullptr;
}

using NativePointer = std::int32_t*;

template<typename T>
auto deduces_readonly_native_slot(T) noexcept -> bool {
    return std::is_same_v<T, NativePointer const*>;
}

inline NativePointer native_slot = &first;

inline auto nested_native() noexcept -> NativePointer* {
    return &native_slot;
}
}
