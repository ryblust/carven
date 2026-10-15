#include "demo/native.hpp"

#include <cstdio>

namespace {

struct Lifetime final {
    Lifetime() noexcept { std::puts("native initialized"); }

    ~Lifetime() noexcept { std::puts("native destroyed"); }
};

const auto lifetime = Lifetime();

} // namespace

auto native_answer() noexcept -> std::int32_t {
    return 43;
}
