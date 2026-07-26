module carven:semantic.hir.constant;

import :semantic.hir.ids;
import :semantic.hir.type;
import std;

class HIRIntegerConstant final {
public:
    static auto zero() noexcept -> HIRIntegerConstant;
    static auto from_signed(std::int64_t source) noexcept -> HIRIntegerConstant;
    static auto from_parts(std::uint64_t magnitude, bool negative) noexcept -> HIRIntegerConstant;
    auto magnitude() const noexcept -> std::uint64_t;
    auto negative() const noexcept -> bool;
    auto as_signed() const noexcept -> std::optional<std::int64_t>;
    auto as_unsigned() const noexcept -> std::optional<std::uint64_t>;

    constexpr auto operator==(const HIRIntegerConstant&) const noexcept -> bool = default;

private:
    HIRIntegerConstant(std::uint64_t magnitude, bool negative) noexcept;

    std::uint64_t stored_magnitude;
    bool stored_negative;
};

struct HIRBooleanConstant final {
    bool value;
    constexpr auto operator==(const HIRBooleanConstant&) const noexcept -> bool = default;
};

struct HIRStringConstant final {
    ProgramSpellingID value;
    constexpr auto operator==(const HIRStringConstant&) const noexcept -> bool = default;
};

struct HIRFloatingConstant final {
    double value;
    constexpr auto operator==(const HIRFloatingConstant&) const noexcept -> bool = default;
};

struct HIRCharacterConstant final {
    char32_t scalar;
    constexpr auto operator==(const HIRCharacterConstant&) const noexcept -> bool = default;
};

struct HIRNumericEnumConstant final {
    EnumCaseID enum_case;
    HIRIntegerConstant value;
};

struct HIRPayloadEnumConstant final {
    EnumCaseID enum_case;
    std::vector<HIRConstantID> payload;
};

using HIRConstant = std::variant<
    HIRIntegerConstant,
    HIRBooleanConstant,
    HIRStringConstant,
    HIRFloatingConstant,
    HIRCharacterConstant,
    HIRNumericEnumConstant,
    HIRPayloadEnumConstant>;

struct HIRConstantFact final {
    HIRTypeID type;
    HIRConstant value;
};

auto normalize_integer_cast(HIRIntegerConstant source, HIRBuiltinType target) noexcept
    -> HIRIntegerConstant;
auto integer_constant_fits(HIRIntegerConstant constant, HIRBuiltinType type) noexcept -> bool;

template<typename Reader>
concept ConstantFactReader = requires (const Reader& reader, HIRConstantID id) {
    { reader.constant(id) } noexcept -> std::same_as<const HIRConstantFact&>;
};

template<ConstantFactReader Reader>
auto constant_equal(const Reader& reader, HIRConstantID left, HIRConstantID right) noexcept
    -> bool {
    auto pending = std::vector<std::pair<HIRConstantID, HIRConstantID>> {{left, right}};
    auto compared = std::flat_set<std::pair<HIRConstantID, HIRConstantID>>();
    while (!pending.empty()) {
        const auto [left_id, right_id] = pending.back();
        pending.pop_back();
        if (left_id == right_id) {
            continue;
        }
        if (!compared.emplace(left_id, right_id).second) {
            continue;
        }

        const auto& left_fact = reader.constant(left_id);
        const auto& right_fact = reader.constant(right_id);
        if (left_fact.type != right_fact.type
            || left_fact.value.index() != right_fact.value.index()) {
            return false;
        }

        const auto equal = std::visit(
            [&](const auto& left_value) noexcept -> bool {
                using Value = std::remove_cvref_t<decltype(left_value)>;
                const auto& right_value = std::get<Value>(right_fact.value);
                if constexpr (std::same_as<Value, HIRNumericEnumConstant>) {
                    return left_value.enum_case == right_value.enum_case
                        && left_value.value == right_value.value;
                } else if constexpr (std::same_as<Value, HIRPayloadEnumConstant>) {
                    if (left_value.enum_case != right_value.enum_case
                        || left_value.payload.size() != right_value.payload.size()) {
                        return false;
                    }
                    for (auto index = 0uz; index < left_value.payload.size(); ++index) {
                        pending.emplace_back(left_value.payload[index], right_value.payload[index]);
                    }
                    return true;
                } else {
                    return left_value == right_value;
                }
            },
            left_fact.value
        );
        if (!equal) {
            return false;
        }
    }
    return true;
}
