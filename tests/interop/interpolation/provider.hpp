#pragma once

#include <format>
#include <string_view>
#include <type_traits>

namespace interpolation_probe {
inline int live = 0;
inline int formatted = 0;
inline int observed_live = 0;

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

// This native API is unrelated to runtime argument adaptation.
inline auto format_argument(const Value&) noexcept -> std::string_view {
    return "native-adaptation";
}

inline auto make(int value) noexcept -> Value {
    return Value(value);
}

inline auto alive() noexcept -> int {
    return live;
}

inline auto calls() noexcept -> int {
    return formatted;
}

inline auto record_live() noexcept -> bool {
    observed_live = live;
    return true;
}

inline auto observed() noexcept -> int {
    return observed_live;
}

struct ContextValue final {};

inline auto context_value() noexcept -> ContextValue {
    return {};
}

struct Unformatted final {};

inline auto unformatted() noexcept -> Unformatted {
    return {};
}
}

template<>
struct std::formatter<interpolation_probe::Value> final : std::formatter<int> {
    constexpr auto parse(std::format_parse_context& context) {
        if (context.begin() != context.end()
            && (*context.begin() == 'q' || *context.begin() == '{')) {
            context.advance_to(context.begin() + 1);
        }
        return std::formatter<int>::parse(context);
    }

    auto format(const interpolation_probe::Value& value, std::format_context& context) const {
        ++interpolation_probe::formatted;
        return std::formatter<int>::format(value.value, context);
    }
};

template<>
struct std::formatter<interpolation_probe::ContextValue> final : std::formatter<int> {
    auto format(const interpolation_probe::ContextValue&, std::format_context& context) const {
        const auto first = std::visit_format_arg(
            [](auto value) noexcept -> int {
                if constexpr (std::is_same_v<decltype(value), int>) {
                    return value;
                } else {
                    return -1;
                }
            },
            context.arg(0)
        );
        return std::formatter<int>::format(first, context);
    }
};
