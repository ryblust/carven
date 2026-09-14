module carven:semantic.evaluation.value;

import :semantic.semir.constant;
import :semantic.semir.constant_access;
import std;

// Execution atoms contain no aggregate storage; compounds have explicit owners below.
using ConstantAtomValue = std::variant<
    IntegerConstant,
    F32Constant,
    F64Constant,
    BooleanConstant,
    CharacterConstant,
    StringConstant,
    NullPointerConstant,
    NumericEnumConstant>;

struct ConstantAtom final {
    TypeID type;
    ConstantAtomValue value;
};

auto constant_atom(const ConstantFact& fact) noexcept -> std::optional<ConstantAtom>;
auto constant_fact(const ConstantAtom& atom) noexcept -> ConstantFact;

struct ConstantOwnedText final {
    std::string bytes;
};

// Immutable execution text owns its bytes without publishing a spelling.
struct ConstantText final {
    std::shared_ptr<const std::string> bytes;
};

struct ConstantVoid final {};

struct ConstantExecutionValue;

struct ConstantAggregateValue final {
    TypeID type;
    std::vector<ConstantExecutionValue> elements;
};

struct ConstantEnumValue final {
    TypeID type;
    EnumCaseID enum_case;
    std::vector<ConstantExecutionValue> payload;
};

// IDs borrow retained inputs; newly computed facts and text remain execution-local.
struct ConstantExecutionValue final : std::variant<
                                          ConstantID,
                                          ConstantAtom,
                                          ConstantText,
                                          ConstantOwnedText,
                                          ConstantVoid,
                                          ConstantAggregateValue,
                                          ConstantEnumValue> {
    using variant::variant;
    using variant::operator=;
};

auto constant_execution_atom(
    const ConstantValueReader& values,
    const ConstantExecutionValue& value
) noexcept -> std::optional<ConstantAtom>;
// Views borrow the retained value or execution owner; mutation ends the borrow.
auto constant_execution_text(
    const ConstantValueReader& values,
    const ConstantExecutionValue& value
) noexcept -> std::optional<std::string_view>;
auto constant_execution_equal(
    const ConstantValueReader& values,
    const ConstantExecutionValue& left,
    const ConstantExecutionValue& right,
    std::size_t& steps,
    std::size_t maximum_steps
) noexcept -> std::optional<bool>;

auto constant_execution_value_type(
    ConstantValueAccess& values,
    const ConstantExecutionValue& value
) noexcept -> TypeID;

// A stable read view over either retained children or execution-owned slots.
struct ConstantCompoundView final {
    auto size() const noexcept -> std::size_t;
    TypeID type;
    std::optional<EnumCaseID> enum_case;
    std::variant<std::span<const ConstantID>, std::span<const ConstantExecutionValue>> elements;
};

auto constant_compound_view(
    const ConstantValueReader& values,
    const ConstantExecutionValue& value
) noexcept -> std::optional<ConstantCompoundView>;
auto constant_execution_elements(ConstantExecutionValue& value) noexcept
    -> std::span<ConstantExecutionValue>;
