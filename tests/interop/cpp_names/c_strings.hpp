#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace c_strings {
inline const char* retained = nullptr;

inline auto same(const char* left, const char* right) noexcept -> bool {
    return std::strcmp(left, right) == 0;
}

template<typename T>
inline auto pointer_value(T&&) noexcept -> bool {
    return std::is_same_v<std::remove_cvref_t<T>, const char*>;
}

template<typename T>
inline auto const_pointer_reference(T&&) noexcept -> bool {
    return std::is_same_v<T, const char* const&>;
}

inline auto remember(const char* value) noexcept -> void {
    retained = value;
}

inline auto remembered() noexcept -> bool {
    return same(retained, "retained");
}

inline auto unicode(const char* value) noexcept -> bool {
    return same(value, "\344\275\240\345\245\275");
}

inline auto select(std::int32_t) noexcept -> std::int32_t {
    return 1;
}

inline auto select(double) noexcept -> std::int32_t {
    return 2;
}

struct Box final {
    std::int32_t value;
};
}

namespace c_other {
inline auto select(std::int32_t) noexcept -> std::int32_t {
    return 3;
}
}
