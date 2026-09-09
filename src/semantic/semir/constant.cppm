module carven:semantic.semir.constant;

import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.table;
import :semantic.semir.type;
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

struct BooleanConstant final {
    bool value;
    constexpr auto operator==(const BooleanConstant&) const noexcept -> bool = default;
};

struct StringConstant final {
    ProgramSpellingID value;
    constexpr auto operator==(const StringConstant&) const noexcept -> bool = default;
};

struct F32Constant final {
    float value;

    constexpr auto operator==(const F32Constant& other) const noexcept -> bool {
        return std::bit_cast<std::uint32_t>(value) == std::bit_cast<std::uint32_t>(other.value);
    }
};

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

using ConstantValue = std::variant<
    IntegerConstant,
    BooleanConstant,
    StringConstant,
    F32Constant,
    F64Constant,
    CharacterConstant,
    NumericEnumConstant,
    PayloadEnumConstant>;

struct ConstantFact final {
    TypeID type;
    ConstantValue value;
    auto operator==(const ConstantFact&) const noexcept -> bool = default;
};

auto normalize_integer_cast(IntegerConstant source, BuiltinType target) noexcept -> IntegerConstant;
auto integer_constant_fits(IntegerConstant constant, BuiltinType type) noexcept -> bool;

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
    auto copy(ConstantID id) const noexcept -> ConstantFact;
    auto owner() const noexcept -> ProgramIdentity;
    auto seal() && noexcept -> ConstantStore;

private:
    ProvenanceIdentity provenance_identity;
    MutableProgramTable<ConstantFact, ConstantID> rows;
};
