#pragma once

#include <format>
#include <type_traits>

namespace formatted_append_probe {

inline int live = 0;
inline int trace = 0;
inline int observed_live = 0;
inline int formatted = 0;
inline int formatting_trace = 0;

inline auto reset() noexcept -> void {
    trace = 0;
    observed_live = 0;
    formatted = 0;
    formatting_trace = 0;
}

inline auto select() noexcept -> int {
    trace = trace * 10 + 1;
    return 1;
}

class Value final {
public:
    explicit Value(int source) noexcept
        : value(source) {
        ++live;
    }

    Value(const Value& source) noexcept
        : value(source.value) {
        ++live;
    }

    Value(Value&& source) noexcept
        : value(source.value) {
        ++live;
    }

    auto operator=(const Value&) -> Value& = delete;
    auto operator=(Value&&) -> Value& = delete;

    ~Value() noexcept { --live; }

    int value;
};

inline auto make(int value) noexcept -> Value {
    trace = trace * 10 + 2;
    return Value(value);
}

inline auto record() noexcept -> int {
    trace = trace * 10 + 3;
    observed_live = live;
    return live;
}

inline auto alive() noexcept -> int {
    return live;
}

inline auto events() noexcept -> int {
    return trace;
}

inline auto observed() noexcept -> int {
    return observed_live;
}

inline auto calls() noexcept -> int {
    return formatted;
}

inline auto at_format() noexcept -> int {
    return formatting_trace;
}

struct ContextValue final {};

inline auto context_value() noexcept -> ContextValue {
    return {};
}

}

template<>
struct std::formatter<formatted_append_probe::Value> final : std::formatter<int> {
    constexpr auto parse(std::format_parse_context& context) {
        if (context.begin() != context.end() && *context.begin() == 'q') {
            context.advance_to(context.begin() + 1);
        }
        return std::formatter<int>::parse(context);
    }

    auto format(const formatted_append_probe::Value& value, std::format_context& context) const {
        ++formatted_append_probe::formatted;
        formatted_append_probe::formatting_trace = formatted_append_probe::trace;
        return std::formatter<int>::format(value.value, context);
    }
};

template<>
struct std::formatter<formatted_append_probe::ContextValue> final : std::formatter<int> {
    auto format(const formatted_append_probe::ContextValue&, std::format_context& context) const {
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
