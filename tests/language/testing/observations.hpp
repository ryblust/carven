#pragma once

#include <cstdint>
#include <cstdio>

namespace observations {

class Owner final {
public:
    explicit Owner(std::int32_t value) noexcept
        : value(value) {}

    Owner(const Owner&) = delete;
    auto operator=(const Owner&) -> Owner& = delete;

    ~Owner() { std::printf("closed %d\n", static_cast<int>(value)); }

private:
    std::int32_t value;
};

} // namespace observations
