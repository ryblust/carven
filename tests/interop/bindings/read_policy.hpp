#pragma once

#include <cstdint>

namespace read_policy {

struct Trivial final {
    std::int32_t value;
};

struct Borrowed {
    std::int32_t value;

    ~Borrowed() {}
};

struct Derived final : Borrowed {};

struct MoveOnly final {
    std::int32_t value;

    explicit MoveOnly(std::int32_t initial) noexcept
        : value(initial) {}

    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) = default;
    auto operator=(const MoveOnly&) -> MoveOnly& = delete;
    auto operator=(MoveOnly&&) -> MoveOnly& = default;
};

struct FreshTarget final {
    std::int32_t value;
};

struct FreshSource final {
    std::int32_t value;

    operator FreshTarget() && noexcept { return FreshTarget {value}; }

    operator FreshTarget() const& noexcept { return FreshTarget {value + 100}; }

    operator FreshTarget() & noexcept { return FreshTarget {value + 200}; }
};

inline auto make_fresh() noexcept -> FreshSource {
    return FreshSource {17};
}

inline auto trivial_reference(Trivial& value) noexcept -> Trivial& {
    return value;
}

inline auto borrowed_reference(Borrowed& value) noexcept -> Borrowed& {
    return value;
}

} // namespace read_policy
