#pragma once

#include <cstdint>

namespace async {

struct match final {
    std::int32_t var;

    auto await() const noexcept -> std::int32_t { return var + 1; }

    auto cancel() const noexcept -> std::int32_t { return var + 2; }
};

inline auto create(std::int32_t value) noexcept -> match {
    return {value};
}

inline auto test() noexcept -> std::int32_t {
    return 42;
}

} // namespace async
