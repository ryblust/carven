module carven:semantic.semir.constant.impl;

import :semantic.semir.constant;
import :support.invariant;
import :support.utf8;
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
    : program_identity(owner),
      provenance_identity(provenance) {}

auto valid_cstring_bytes(std::string_view bytes) noexcept -> bool {
    return !bytes.contains('\0') && UTF8Decoder::is_valid(bytes);
}

auto constant_children(const ConstantValue& value) noexcept
    -> std::optional<std::span<const ConstantID>> {
    return value.visit(
        [](const auto& stored) static noexcept -> std::optional<std::span<const ConstantID>> {
            using Value = std::remove_cvref_t<decltype(stored)>;
            if constexpr (std::same_as<Value, PayloadEnumConstant>) {
                return stored.payload;
            } else if constexpr (std::same_as<Value, StructConstant>) {
                return stored.fields;
            } else if constexpr (std::same_as<Value, ArrayConstant>
                                 || std::same_as<Value, SliceConstant>) {
                return stored.elements;
            }
            return std::nullopt;
        }
    );
}

auto ConstantStoreBuilder::intern(ConstantFact fact) noexcept -> ConstantID {
    require_owner(fact.type.owner(), program_identity, "constant fact used a foreign type");
    auto hash = static_cast<std::size_t>(fact.type.index());
    const auto mix = [&](std::uint64_t value) noexcept {
        hash ^=
            std::hash<std::uint64_t>()(value) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    };
    mix(fact.value.index());
    fact.value.visit([&](const auto& value) noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, StringConstant> || std::same_as<Value, CStringConstant>) {
            if (value.value.owner() != provenance_identity) {
                invariant_violation("string constant used a foreign spelling");
            }
            mix(value.value.index());
        } else if constexpr (std::same_as<Value, RangeConstant>) {
            mix(value.begin.magnitude());
            mix(value.begin.negative());
            mix(value.end.magnitude());
            mix(value.end.negative());
            mix(value.inclusive);
        } else if constexpr (std::same_as<Value, IntegerConstant>) {
            mix(value.magnitude());
            mix(value.negative());
        } else if constexpr (std::same_as<Value, BooleanConstant>) {
            mix(value.value);
        } else if constexpr (std::same_as<Value, CharacterConstant>) {
            mix(value.scalar);
        } else if constexpr (std::same_as<Value, F32Constant>) {
            mix(std::bit_cast<std::uint32_t>(value.value));
        } else if constexpr (std::same_as<Value, F64Constant>) {
            mix(std::bit_cast<std::uint64_t>(value.value));
        } else if constexpr (std::same_as<Value, NumericEnumConstant>
                             || std::same_as<Value, PayloadEnumConstant>) {
            require_owner(
                value.enum_case.owner(),
                program_identity,
                "constant used a foreign enum case"
            );
            mix(value.enum_case.index());
            if constexpr (std::same_as<Value, NumericEnumConstant>) {
                mix(value.value.magnitude());
                mix(value.value.negative());
            }
        }
    });
    if (const auto children = constant_children(fact.value)) {
        for (const auto child : *children) {
            static_cast<void>(constant(child));
            mix(child.index());
        }
    }
    const auto [begin, end] = index.equal_range(hash);
    for (auto entry = begin; entry != end; ++entry) {
        if (constant(entry->second) == fact) {
            return entry->second;
        }
    }
    if (rows.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("constant store exhausted its 32-bit identity space");
    }
    const auto id = ConstantID(program_identity, static_cast<std::uint32_t>(rows.size()));
    rows.push_back(std::move(fact));
    index.emplace(hash, id);
    return id;
}

auto ConstantStoreBuilder::constant(ConstantID id) const noexcept -> const ConstantFact& {
    if (id.owner() != program_identity || id.index() >= rows.size()) {
        invariant_violation("constant lookup used a foreign or unavailable constant");
    }
    return rows[id.index()];
}

auto ConstantStoreBuilder::owner() const noexcept -> ProgramIdentity {
    return program_identity;
}

auto ConstantStoreBuilder::seal() && noexcept -> ConstantStore {
    auto sealed = MutableProgramTable<ConstantFact, ConstantID>(program_identity);
    for (auto& fact : rows) {
        sealed.add(std::move(fact));
    }
    return ConstantStore(std::move(sealed).seal());
}
