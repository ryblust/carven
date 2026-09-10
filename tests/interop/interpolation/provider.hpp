#pragma once

#include <format>
#include <string_view>

namespace interpolation_probe {
inline int live = 0;
inline int formatted = 0;

class Value final {
public:
    explicit Value(int value) noexcept
        : value(value) {
        ++live;
    }

    Value(const Value& other) noexcept
        : value(other.value) {
        ++live;
    }

    Value(Value&& other) noexcept
        : value(other.value) {
        ++live;
    }

    auto operator=(const Value&) -> Value& = delete;
    auto operator=(Value&&) -> Value& = delete;

    ~Value() noexcept { --live; }

    int value;
};

inline auto make(int value) noexcept -> Value {
    return Value(value);
}

inline auto alive() noexcept -> int {
    return live;
}

inline auto calls() noexcept -> int {
    return formatted;
}

struct Unformatted final {};

inline auto unformatted() noexcept -> Unformatted {
    return {};
}
}

template<>
struct std::formatter<interpolation_probe::Value> final : std::formatter<int> {
    constexpr auto parse(std::format_parse_context& context) {
        if (context.begin() != context.end() && *context.begin() == 'q') {
            context.advance_to(context.begin() + 1);
        }
        return std::formatter<int>::parse(context);
    }

    auto format(const interpolation_probe::Value& value, std::format_context& context) const {
        ++interpolation_probe::formatted;
        return std::formatter<int>::format(value.value, context);
    }
};
