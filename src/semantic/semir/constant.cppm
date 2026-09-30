module carven:semantic.semir.constant;

import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.simd;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.provenance;
import :source.provenance.ids;
import std;

class IntegerConstant final {
public:
    static auto zero() noexcept -> IntegerConstant;
    static auto from_signed(std::int64_t source) noexcept -> IntegerConstant;
    static auto from_parts(std::uint64_t magnitude, bool negative) noexcept -> IntegerConstant;
    auto magnitude() const noexcept -> std::uint64_t;
    auto negative() const noexcept -> bool;
    auto as_signed() const noexcept -> std::optional<std::int64_t>;
    auto as_unsigned() const noexcept -> std::optional<std::uint64_t>;
    constexpr auto operator==(const IntegerConstant&) const noexcept -> bool = default;

private:
    IntegerConstant(std::uint64_t magnitude, bool negative) noexcept;

    std::uint64_t stored_magnitude;
    bool stored_negative;
};

struct RangeConstant final {
    IntegerConstant begin;
    IntegerConstant end;
    bool inclusive;
    constexpr auto operator==(const RangeConstant&) const noexcept -> bool = default;
};

struct NullPointerConstant final {
    constexpr auto operator==(const NullPointerConstant&) const noexcept -> bool = default;
};

// Each lane stores its representation bits. The owning type determines width,
// element interpretation, and mask semantics.
struct SIMDConstant final {
    std::vector<std::uint32_t> lanes;
    auto operator==(const SIMDConstant&) const noexcept -> bool = default;
};

struct BooleanConstant final {
    bool value;
    constexpr auto operator==(const BooleanConstant&) const noexcept -> bool = default;
};

struct StringConstant final {
    ProgramSpellingID value;
    constexpr auto operator==(const StringConstant&) const noexcept -> bool = default;
};

struct CStringConstant final {
    ProgramSpellingID value;
    constexpr auto operator==(const CStringConstant&) const noexcept -> bool = default;
};

auto valid_cstring_bytes(std::string_view bytes) noexcept -> bool;

struct F32Constant final {
    float value;

    constexpr auto operator==(const F32Constant& other) const noexcept -> bool {
        return std::bit_cast<std::uint32_t>(value) == std::bit_cast<std::uint32_t>(other.value);
    }
};

auto matches_simd_constant(BuiltinType owner, const SIMDConstant& value) noexcept -> bool;

struct F64Constant final {
    double value;

    constexpr auto operator==(const F64Constant& other) const noexcept -> bool {
        return std::bit_cast<std::uint64_t>(value) == std::bit_cast<std::uint64_t>(other.value);
    }
};

struct CharacterConstant final {
    char32_t scalar;
    constexpr auto operator==(const CharacterConstant&) const noexcept -> bool = default;
};

struct NumericEnumConstant final {
    EnumCaseID enum_case;
    IntegerConstant value;
    constexpr auto operator==(const NumericEnumConstant&) const noexcept -> bool = default;
};

struct PayloadEnumConstant final {
    EnumCaseID enum_case;
    std::vector<ConstantID> payload;
    auto operator==(const PayloadEnumConstant&) const noexcept -> bool = default;
};

// Fields are stored in declaration order; ConstantFact retains the nominal type.
struct StructConstant final {
    std::vector<ConstantID> fields;
    auto operator==(const StructConstant&) const noexcept -> bool = default;
};

struct ArrayConstant final {
    std::vector<ConstantID> elements;
    auto operator==(const ArrayConstant&) const noexcept -> bool = default;
};

// Frozen contents describe persistent read-only storage without host addresses.
struct SliceConstant final {
    std::vector<ConstantID> elements;
    auto operator==(const SliceConstant&) const noexcept -> bool = default;
};

using ConstantValue = std::variant<
    SIMDConstant,
    IntegerConstant,
    RangeConstant,
    BooleanConstant,
    NullPointerConstant,
    StringConstant,
    CStringConstant,
    F32Constant,
    F64Constant,
    CharacterConstant,
    NumericEnumConstant,
    PayloadEnumConstant,
    StructConstant,
    ArrayConstant,
    SliceConstant>;

struct ConstantFact final {
    TypeID type;
    ConstantValue value;
    auto operator==(const ConstantFact&) const noexcept -> bool = default;
};

auto constant_children(const ConstantValue& value) noexcept
    -> std::optional<std::span<const ConstantID>>;

auto normalize_integer_cast(IntegerConstant source, BuiltinType target) noexcept -> IntegerConstant;
auto integer_constant_fits(IntegerConstant constant, BuiltinType type) noexcept -> bool;

// Readers return optional values so malformed identities fail this contract
// before any store's terminating lookup boundary is crossed.
template<typename Types, typename Constants, typename Fields, typename Cases, typename Spellings>
auto constant_matches_type(
    const ConstantFact& fact,
    Types types,
    Constants constants,
    Fields fields,
    Cases cases,
    Spellings spellings
) noexcept -> bool {
    const auto canonical = types(fact.type);
    if (!canonical) {
        return false;
    }
    const auto& type = canonical->value;
    const auto builtin = [&](BuiltinType expected) noexcept {
        return type == CanonicalTypeValue {BuiltinTypeValue {.kind = expected}};
    };
    const auto children_match = [&](std::span<const ConstantID> children,
                                    std::span<const TypeID> expected) noexcept {
        if (children.size() != expected.size()) {
            return false;
        }
        for (const auto& [child, target] : std::views::zip(children, expected)) {
            const auto value = constants(child);
            if (!value || value->type != target) {
                return false;
            }
        }
        return true;
    };
    return fact.value.visit([&](const auto& value) noexcept -> bool {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, RangeConstant>) {
            const auto* range = std::get_if<RangeTypeValue>(&type);
            if (!range) {
                return false;
            }
            const auto element = types(range->element);
            const auto* kind = element ? std::get_if<BuiltinTypeValue>(&element->value) : nullptr;
            return kind
                && integer_constant_fits(value.begin, kind->kind)
                && integer_constant_fits(value.end, kind->kind);
        } else if constexpr (std::same_as<Value, IntegerConstant>) {
            const auto* kind = std::get_if<BuiltinTypeValue>(&type);
            return kind && integer_constant_fits(value, kind->kind);
        } else if constexpr (std::same_as<Value, SIMDConstant>) {
            const auto* kind = std::get_if<BuiltinTypeValue>(&type);
            return kind && matches_simd_constant(kind->kind, value);
        } else if constexpr (std::same_as<Value, BooleanConstant>) {
            return builtin(BuiltinType::Bool);
        } else if constexpr (std::same_as<Value, F32Constant>) {
            return builtin(BuiltinType::F32);
        } else if constexpr (std::same_as<Value, F64Constant>) {
            return builtin(BuiltinType::F64);
        } else if constexpr (std::same_as<Value, CharacterConstant>) {
            return builtin(BuiltinType::Char)
                && value.scalar <= 0x10ffffu
                && (value.scalar < 0xd800u || value.scalar > 0xdfffu);
        } else if constexpr (std::same_as<Value, NullPointerConstant>) {
            return std::holds_alternative<PointerTypeValue>(type);
        } else if constexpr (std::same_as<Value, StringConstant>) {
            return builtin(BuiltinType::Str) && spellings(value.value).has_value();
        } else if constexpr (std::same_as<Value, CStringConstant>) {
            const auto* native = std::get_if<CppTypeValue>(&type);
            const auto bytes = spellings(value.value);
            return native
                && std::holds_alternative<CppConstCharPointerType>(native->form)
                && bytes
                && valid_cstring_bytes(*bytes);
        } else if constexpr (std::same_as<Value, NumericEnumConstant>
                             || std::same_as<Value, PayloadEnumConstant>) {
            const auto* enumeration = std::get_if<EnumTypeValue>(&type);
            if (!enumeration) {
                return false;
            }
            const auto payload = cases(enumeration->enumeration, value.enum_case);
            if (!payload) {
                return false;
            }
            if constexpr (std::same_as<Value, NumericEnumConstant>) {
                return payload->empty();
            } else {
                return children_match(value.payload, *payload);
            }
        } else if constexpr (std::same_as<Value, StructConstant>) {
            const auto* record = std::get_if<StructTypeValue>(&type);
            if (!record) {
                return false;
            }
            const auto expected = fields(record->structure);
            return expected && children_match(value.fields, *expected);
        } else if constexpr (std::same_as<Value, ArrayConstant>
                             || std::same_as<Value, SliceConstant>) {
            auto element = std::optional<TypeID>();
            if constexpr (std::same_as<Value, ArrayConstant>) {
                const auto* array = std::get_if<ArrayTypeValue>(&type);
                if (!array || array->extent != value.elements.size()) {
                    return false;
                }
                element = array->element;
            } else {
                const auto* slice = std::get_if<SliceTypeValue>(&type);
                if (!slice) {
                    return false;
                }
                element = slice->element;
            }
            for (const auto id : value.elements) {
                const auto child = constants(id);
                if (!child || child->type != *element) {
                    return false;
                }
            }
            return true;
        } else {
            static_assert(std::same_as<Value, void>, "constant type contract is incomplete");
        }
    });
}

class ConstantStore final {
public:
    ConstantStore(const ConstantStore&) = delete;
    ConstantStore(ConstantStore&&) = default;
    ~ConstantStore() = default;
    auto operator=(const ConstantStore&) -> ConstantStore& = delete;
    auto operator=(ConstantStore&&) -> ConstantStore& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(ConstantID id) const noexcept -> bool;
    auto constant(ConstantID id) const noexcept -> const ConstantFact&;
    auto entries() const noexcept -> IDTableEntries<ConstantID, ConstantFact, ProgramIdentity>;
    auto size() const noexcept -> std::size_t;

private:
    explicit ConstantStore(ImmutableProgramTable<ConstantFact, ConstantID> rows) noexcept;

    ImmutableProgramTable<ConstantFact, ConstantID> rows;

    friend class ConstantStoreBuilder;
};

class ConstantStoreBuilder final {
public:
    ConstantStoreBuilder(ProgramIdentity owner, ProvenanceIdentity provenance) noexcept;
    ConstantStoreBuilder(const ConstantStoreBuilder&) = delete;
    ConstantStoreBuilder(ConstantStoreBuilder&&) = default;
    ~ConstantStoreBuilder() = default;
    auto operator=(const ConstantStoreBuilder&) -> ConstantStoreBuilder& = delete;
    auto operator=(ConstantStoreBuilder&&) -> ConstantStoreBuilder& = delete;
    auto intern(ConstantFact fact) noexcept -> ConstantID;
    // Borrows survive interning and end when this owner is moved or sealed.
    auto constant(ConstantID id) const noexcept -> const ConstantFact&;
    auto owner() const noexcept -> ProgramIdentity;
    auto seal() && noexcept -> ConstantStore;

private:
    ProgramIdentity program_identity;
    ProvenanceIdentity provenance_identity;
    std::deque<ConstantFact> rows;
    std::unordered_multimap<std::size_t, ConstantID> index;
};
