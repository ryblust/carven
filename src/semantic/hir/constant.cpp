module carven:semantic.hir.constant.impl;

import :semantic.hir.constant;
import :support.invariant;
import std;

HIRIntegerConstant::HIRIntegerConstant(std::uint64_t magnitude, bool negative) noexcept
    : stored_magnitude(magnitude),
      stored_negative(negative && magnitude != 0) {}

auto HIRIntegerConstant::zero() noexcept -> HIRIntegerConstant {
    return HIRIntegerConstant(0, false);
}

auto HIRIntegerConstant::from_signed(std::int64_t source) noexcept -> HIRIntegerConstant {
    if (source >= 0) {
        return HIRIntegerConstant(static_cast<std::uint64_t>(source), false);
    }
    return HIRIntegerConstant(static_cast<std::uint64_t>(-(source + 1)) + 1, true);
}

auto HIRIntegerConstant::from_parts(std::uint64_t magnitude, bool negative) noexcept
    -> HIRIntegerConstant {
    return HIRIntegerConstant(magnitude, negative);
}

auto HIRIntegerConstant::magnitude() const noexcept -> std::uint64_t {
    return stored_magnitude;
}

auto HIRIntegerConstant::negative() const noexcept -> bool {
    return stored_negative;
}

auto HIRIntegerConstant::as_signed() const noexcept -> std::optional<std::int64_t> {
    constexpr auto minimum_magnitude = 1ull << 63;
    if (!stored_negative) {
        if (stored_magnitude
            > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(stored_magnitude);
    }
    if (stored_magnitude > minimum_magnitude) {
        return std::nullopt;
    }
    if (stored_magnitude == minimum_magnitude) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return -static_cast<std::int64_t>(stored_magnitude);
}

auto HIRIntegerConstant::as_unsigned() const noexcept -> std::optional<std::uint64_t> {
    return stored_negative ? std::nullopt : std::optional<std::uint64_t> {stored_magnitude};
}

auto normalize_integer_cast(HIRIntegerConstant source, HIRBuiltinType target) noexcept
    -> HIRIntegerConstant {
    const auto width = builtin_integer_width(target);
    if (!width.has_value()) {
        invariant_violation("integer cast normalization requires an integer target type");
    }
    const auto mask =
        *width == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ull << *width) - 1;
    auto bits = source.negative() ? 0ull - source.magnitude() : source.magnitude();
    bits &= mask;
    if (!builtin_is_signed_integer(target) || (bits & (1ull << (*width - 1))) == 0) {
        return HIRIntegerConstant::from_parts(bits, false);
    }
    const auto magnitude = *width == 64 ? (~bits) + 1 : ((~bits) & mask) + 1;
    return HIRIntegerConstant::from_parts(magnitude, true);
}

auto integer_constant_fits(HIRIntegerConstant constant, HIRBuiltinType type) noexcept -> bool {
    const auto width = builtin_integer_width(type);
    if (!width.has_value()) {
        return false;
    }
    if (!builtin_is_signed_integer(type)) {
        if (constant.negative()) {
            return false;
        }
        const auto maximum =
            *width == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ull << *width) - 1;
        return constant.magnitude() <= maximum;
    }
    const auto minimum_magnitude = 1ull << (*width - 1);
    const auto maximum = minimum_magnitude - 1;
    return constant.negative() ? constant.magnitude() <= minimum_magnitude
                               : constant.magnitude() <= maximum;
}
