#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace construction_probe {

template<int Mode>
struct Selected final {
    template<typename T>
    explicit Selected(T&&) noexcept {}

    auto mode() const noexcept -> int { return Mode; }
};

Selected(const int&) -> Selected<1>;
Selected(int&) -> Selected<2>;
Selected(int&&) -> Selected<3>;

template<typename T>
struct Box final {
    T value;

    auto read() const noexcept -> const T& { return value; }
};

template<typename T>
Box(const T&) -> Box<T>;
template<typename T>
Box(T&&) -> Box<std::remove_cvref_t<T>>;

inline auto resource() noexcept -> std::unique_ptr<int> {
    return std::make_unique<int>(42);
}

inline auto read_resource(const std::unique_ptr<int>& value) noexcept -> int {
    return *value;
}

inline int trace = 0;

inline auto record(int digit) noexcept -> int {
    trace = trace * 10 + digit;
    return digit;
}

inline auto reset() noexcept -> void {
    trace = 0;
}

inline auto recorded() noexcept -> int {
    return trace;
}

struct Byte final {
    unsigned char value;
};

struct Float final {
    float value;
};
} // namespace construction_probe
