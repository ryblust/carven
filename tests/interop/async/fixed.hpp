#pragma once

#include <cstdint>

// A result that can only be constructed at its final owning address.
class Fixed final {
public:
    Fixed() noexcept;
    Fixed(const Fixed&) = delete;
    Fixed(Fixed&&) = delete;
    auto operator=(const Fixed&) -> Fixed& = delete;
    auto operator=(Fixed&&) -> Fixed& = delete;
    ~Fixed() noexcept;
    auto at_construction_address() const noexcept -> bool;

private:
    const Fixed* construction_address;
};

// No borrowed source endpoint crosses these native diagnostic calls.
auto make_fixed() noexcept -> Fixed;

auto fixed_created() noexcept -> std::int32_t;
auto fixed_destroyed() noexcept -> std::int32_t;
auto fixed_live() noexcept -> std::int32_t;
auto fixed_trace() noexcept -> std::int32_t;
auto fixed_identity_stable() noexcept -> std::int32_t;
auto fixed_report() noexcept -> void;
