module carven:semantic.semir.constant.impl;

import :semantic.semir.constant;
import :support.invariant;
import std;

namespace {

auto require_owner(ProgramIdentity owner, ProgramIdentity expected, std::string_view fact) noexcept
    -> void {
    if (owner != expected) {
        invariant_violation(fact);
    }
}

} // namespace

IntegerConstant::IntegerConstant(std::uint64_t magnitude, bool negative) noexcept
    : stored_magnitude(magnitude),
      stored_negative(negative && magnitude != 0u) {}

auto IntegerConstant::zero() noexcept -> IntegerConstant {
    return IntegerConstant(0u, false);
}

auto IntegerConstant::from_signed(std::int64_t source) noexcept -> IntegerConstant {
    if (source >= 0) {
        return IntegerConstant(static_cast<std::uint64_t>(source), false);
    }
    const auto adjusted = static_cast<std::uint64_t>(-(source + 1));
    return IntegerConstant(adjusted + 1u, true);
}

auto IntegerConstant::from_parts(std::uint64_t magnitude, bool negative) noexcept
    -> IntegerConstant {
    return IntegerConstant(magnitude, negative);
}

auto IntegerConstant::magnitude() const noexcept -> std::uint64_t {
    return stored_magnitude;
}

auto IntegerConstant::negative() const noexcept -> bool {
    return stored_negative;
}

auto IntegerConstant::as_signed() const noexcept -> std::optional<std::int64_t> {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (!stored_negative) {
        if (stored_magnitude > maximum) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(stored_magnitude);
    }
    if (stored_magnitude > maximum + 1u) {
        return std::nullopt;
    }
    if (stored_magnitude == maximum + 1u) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return -static_cast<std::int64_t>(stored_magnitude);
}

auto IntegerConstant::as_unsigned() const noexcept -> std::optional<std::uint64_t> {
    return stored_negative ? std::nullopt : std::optional<std::uint64_t>(stored_magnitude);
}

auto normalize_integer_cast(IntegerConstant source, BuiltinType target) noexcept
    -> IntegerConstant {
    const auto width = builtin_integer_width(target);
    if (!width.has_value()) {
        invariant_violation("integer normalization target was not an integer type");
    }
    const auto bits = *width;
    const auto mask =
        bits == 64u ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t {1u} << bits) - 1u;
    auto value = source.magnitude() & mask;
    if (source.negative()) {
        value = (~value + 1u) & mask;
    }
    if (!builtin_is_signed_integer(target)) {
        return IntegerConstant::from_parts(value, false);
    }
    const auto sign_bit = std::uint64_t {1u} << (bits - 1u);
    if ((value & sign_bit) == 0u) {
        return IntegerConstant::from_parts(value, false);
    }
    const auto magnitude = (~value + 1u) & mask;
    return IntegerConstant::from_parts(magnitude, true);
}

auto integer_constant_fits(IntegerConstant constant, BuiltinType type) noexcept -> bool {
    const auto width = builtin_integer_width(type);
    if (!width.has_value()) {
        return false;
    }
    if (builtin_is_signed_integer(type)) {
        const auto maximum = *width == 64u
            ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
            : (std::uint64_t {1u} << (*width - 1u)) - 1u;
        return constant.negative() ? constant.magnitude() <= maximum + 1u
                                   : constant.magnitude() <= maximum;
    }
    const auto maximum = *width == 64u ? std::numeric_limits<std::uint64_t>::max()
                                       : (std::uint64_t {1u} << *width) - 1u;
    return !constant.negative() && constant.magnitude() <= maximum;
}

ConstantStore::ConstantStore(ImmutableProgramTable<ConstantFact, ConstantID> values) noexcept
    : rows(std::move(values)) {}

auto ConstantStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto ConstantStore::contains(ConstantID id) const noexcept -> bool {
    return rows.contains(id);
}

auto ConstantStore::constant(ConstantID id) const noexcept -> const ConstantFact& {
    return rows.get(id);
}

auto ConstantStore::entries() const noexcept
    -> IDTableEntries<ConstantID, ConstantFact, ProgramIdentity> {
    return rows.entries();
}

auto ConstantStore::size() const noexcept -> std::size_t {
    return rows.size();
}

ConstantStoreBuilder::ConstantStoreBuilder(
    ProgramIdentity owner,
    ProvenanceIdentity provenance
) noexcept
    : provenance_identity(provenance),
      rows(owner) {}

auto ConstantStoreBuilder::intern(ConstantFact fact) noexcept -> ConstantID {
    require_owner(fact.type.owner(), rows.owner(), "constant fact used a foreign type");
    std::visit(
        [this](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, StringConstant>) {
                if (value.value.owner() != provenance_identity) {
                    invariant_violation("string constant used a foreign spelling");
                }
            } else if constexpr (std::same_as<Value, NumericEnumConstant>) {
                require_owner(
                    value.enum_case.owner(),
                    rows.owner(),
                    "numeric enum constant used a foreign enum case"
                );
            } else if constexpr (std::same_as<Value, PayloadEnumConstant>) {
                require_owner(
                    value.enum_case.owner(),
                    rows.owner(),
                    "payload enum constant used a foreign enum case"
                );
                for (const auto child : value.payload) {
                    require_owner(
                        child.owner(),
                        rows.owner(),
                        "payload enum constant used a foreign constant"
                    );
                }
            } else {
                static_assert(
                    std::same_as<Value, IntegerConstant>
                    || std::same_as<Value, BooleanConstant>
                    || std::same_as<Value, NullPointerConstant>
                    || std::same_as<Value, F32Constant>
                    || std::same_as<Value, F64Constant>
                    || std::same_as<Value, CharacterConstant>
                );
            }
        },
        fact.value
    );
    return rows.intern(std::move(fact));
}

auto ConstantStoreBuilder::copy(ConstantID id) const noexcept -> ConstantFact {
    return rows.copy(id);
}

auto ConstantStoreBuilder::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto ConstantStoreBuilder::seal() && noexcept -> ConstantStore {
    return ConstantStore(std::move(rows).seal());
}
