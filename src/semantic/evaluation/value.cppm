module carven:semantic.evaluation.value;

import :semantic.semir.constant;
import :semantic.semir.constant_access;
import std;

// Execution atoms contain no aggregate storage; compounds have explicit owners below.
using ConstantAtomValue = std::variant<
    IntegerConstant,
    RangeConstant,
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

struct ExecutionOwnedText final {
    std::string bytes;
};

// Immutable execution text owns its bytes without publishing a spelling.
struct ExecutionText final {
    std::shared_ptr<const std::string> bytes;
};

struct ExecutionVoid final {};

struct ExecutionValue;

struct ExecutionAggregateValue final {
    TypeID type;
    std::vector<ExecutionValue> elements;
};

struct ExecutionEnumValue final {
    TypeID type;
    EnumCaseID enum_case;
    std::vector<ExecutionValue> payload;
};

// IDs borrow retained inputs; newly computed facts and text remain execution-local.
struct ExecutionValue final : std::variant<
                                  ConstantID,
                                  ConstantAtom,
                                  ExecutionText,
                                  ExecutionOwnedText,
                                  ExecutionVoid,
                                  ExecutionAggregateValue,
                                  ExecutionEnumValue> {
    using variant::variant;
    using variant::operator=;
};

auto execution_atom(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<ConstantAtom>;
// Views borrow the retained value or execution owner; mutation ends the borrow.
auto execution_text(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<std::string_view>;
auto execution_equal(
    const ConstantValueReader& values,
    const ExecutionValue& left,
    const ExecutionValue& right,
    std::size_t& steps,
    std::size_t maximum_steps
) noexcept -> std::optional<bool>;

auto execution_value_type(ExecutionValueAccess& values, const ExecutionValue& value) noexcept
    -> TypeID;

// A stable read view over either retained children or execution-owned slots.
struct ExecutionCompoundView final {
    auto size() const noexcept -> std::size_t;
    TypeID type;
    std::optional<EnumCaseID> enum_case;
    std::variant<std::span<const ConstantID>, std::span<const ExecutionValue>> elements;
};

auto execution_compound_view(
    const ConstantValueReader& values,
    const ExecutionValue& value
) noexcept -> std::optional<ExecutionCompoundView>;
auto execution_elements(ExecutionValue& value) noexcept -> std::span<ExecutionValue>;
