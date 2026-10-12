#pragma once

namespace generic_native {

template<typename Value>
struct Box final {
    Value value;

    auto get() const noexcept -> Value { return value; }

    static auto identity(const Value& value) noexcept -> Value { return value; }

    static auto identity(const Value& value, const Value& increment) noexcept -> Value {
        return value + increment;
    }

    static constexpr auto size = sizeof(Value);
};

} // namespace generic_native
