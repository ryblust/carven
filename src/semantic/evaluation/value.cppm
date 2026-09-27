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
    CStringConstant,
    NullPointerConstant,
    NumericEnumConstant>;

struct ConstantAtom final {
    TypeID type;
    ConstantAtomValue value;
};

auto constant_atom(const ConstantFact& fact) noexcept -> std::optional<ConstantAtom>;
auto constant_fact(const ConstantAtom& atom) noexcept -> ConstantFact;

struct ExecutionVoid final {};

struct ExecutionStorageIdentity final {};

// Coordinates survive host container movement, but never retain a target's lifetime.
struct ExecutionPlace final {
    std::shared_ptr<const ExecutionStorageIdentity> owner;
    std::size_t object;
    std::vector<std::size_t> path;

    auto operator==(const ExecutionPlace&) const -> bool = default;
};

// String owns its execution-local content identity. Retained text borrows stable reader bytes.
// Transferring or changing String storage establishes a fresh identity.
struct ExecutionTextStorage final {
    std::variant<std::string, std::string_view> bytes;
    std::optional<ExecutionPlace> byte_backing;
};

struct ExecutionOwnedText final {
    std::shared_ptr<ExecutionTextStorage> storage;
};

// Immutable execution text shares its content; as_str holds only a weak String borrow.
struct ExecutionText final {
    std::variant<std::shared_ptr<ExecutionTextStorage>, std::weak_ptr<ExecutionTextStorage>>
        storage;
};

auto execution_text_storage(const ExecutionText& text) noexcept
    -> std::shared_ptr<ExecutionTextStorage>;
auto make_owned_execution_text(std::string bytes) noexcept -> ExecutionOwnedText;
auto make_execution_text(std::string bytes) noexcept -> std::shared_ptr<ExecutionTextStorage>;
auto execution_text_bytes(const ExecutionTextStorage& storage) noexcept -> std::string_view;

struct ExecutionPointer final {
    TypeID type;
    std::optional<ExecutionPlace> target;
};

// A slice borrows elements of one live array or text backing. Copying it copies the coordinate,
// not its backing storage. A sub-slice advances offset within the same owner.
struct ExecutionSlice final {
    TypeID type;
    ExecutionPlace backing;
    std::size_t offset;
    std::size_t extent;
};

struct ExecutionFunction final {
    ConstructionTypeRef type;
    CallableID callable;
};

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
                                  ExecutionPointer,
                                  ExecutionSlice,
                                  ExecutionFunction,
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
class ExecutionMemory;

enum class ExecutionComparisonFailure { StepLimit, ExpiredText, Unsupported };

auto execution_equal(
    const ConstantValueReader& values,
    const ExecutionValue& left,
    const ExecutionValue& right,
    std::size_t& steps,
    std::size_t maximum_steps
) noexcept -> std::expected<bool, ExecutionComparisonFailure>;

auto execution_value_type(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> ConstructionTypeRef;

// Byte observation constructs scalar values on demand and never allocates an array.
struct ExecutionByteView final {
    TypeID element;
    std::string_view bytes;
    auto size() const noexcept -> std::size_t;
    auto operator[](std::size_t index) const noexcept -> ExecutionValue;
};

using ExecutionSequenceView = std::variant<std::span<const ExecutionValue>, ExecutionByteView>;

auto transfer_owned_text(ExecutionValue& value) noexcept -> void;

// A stable read view over either retained children or execution-owned slots.
struct ExecutionCompoundView final {
    auto size() const noexcept -> std::size_t;
    TypeID type;
    std::optional<EnumCaseID> enum_case;
    std::variant<std::span<const ConstantID>, std::span<const ExecutionValue>, ExecutionByteView>
        elements;
};

auto execution_compound_view(
    const ConstantValueReader& values,
    const ExecutionValue& value,
    const ExecutionMemory* memory = nullptr
) noexcept -> std::optional<ExecutionCompoundView>;
auto execution_elements(ExecutionValue& value) noexcept -> std::span<ExecutionValue>;
