module carven:semantic.evaluation.value;

import :semantic.semir.constant;
import :semantic.semir.constant_access;
import std;

// Execution atoms contain no aggregate storage; compounds have explicit owners below.
using ConstantAtomValue = std::variant<
    SIMDConstant,
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

class ExecutionMemory;

class ExecutionIdentity final {
public:
    constexpr auto operator==(const ExecutionIdentity&) const noexcept -> bool = default;

private:
    explicit constexpr ExecutionIdentity(std::uint64_t value) noexcept
        : value(value) {}

    static auto fresh() noexcept -> ExecutionIdentity;

    std::uint64_t value;

    friend class ExecutionMemory;
};

// Coordinates survive host container movement, but never retain a target's lifetime.
struct ExecutionPlace final {
    ExecutionIdentity owner;
    std::size_t object;
    std::vector<std::size_t> path;

    auto operator==(const ExecutionPlace&) const -> bool = default;
};

class ExecutionOwnedText;

// Storage is private to text handles and memory. Only the String owner can mutate bytes.
class ExecutionTextStorage final {
public:
    explicit ExecutionTextStorage(std::variant<std::string, std::string_view> bytes) noexcept;

private:
    auto view() const noexcept -> std::string_view;
    std::variant<std::string, std::string_view> bytes;

    friend class ExecutionText;
    friend class ExecutionOwnedText;
    friend class ExecutionMemory;
};

// Computed text shares immutable content. A String view retains only a weak borrow.
class ExecutionText final {
public:
    explicit ExecutionText(std::string bytes) noexcept;
    // The reader's spelling storage must outlive every handle and byte observation.
    static auto retained(std::string_view bytes) noexcept -> ExecutionText;
    auto borrow() const noexcept -> ExecutionText;
    auto is_borrowed() const noexcept -> bool;
    // The view ends when its backing is changed or released; it never keeps a String alive.
    auto bytes() const noexcept -> std::optional<std::string_view>;

private:
    explicit ExecutionText(std::shared_ptr<const ExecutionTextStorage> storage) noexcept;
    explicit ExecutionText(std::weak_ptr<const ExecutionTextStorage> storage) noexcept;
    auto lock() const noexcept -> std::shared_ptr<const ExecutionTextStorage>;

    std::variant<
        std::shared_ptr<const ExecutionTextStorage>,
        std::weak_ptr<const ExecutionTextStorage>>
        storage;

    friend class ExecutionOwnedText;
    friend class ExecutionMemory;
};

// Host moves transport an owner; source Take and mutation explicitly end its old borrows.
class ExecutionOwnedText final {
public:
    explicit ExecutionOwnedText(std::string bytes) noexcept;
    ExecutionOwnedText(const ExecutionOwnedText&) = delete;
    ExecutionOwnedText(ExecutionOwnedText&&) = default;
    auto operator=(const ExecutionOwnedText&) -> ExecutionOwnedText& = delete;
    auto operator=(ExecutionOwnedText&&) -> ExecutionOwnedText& = default;
    ~ExecutionOwnedText() = default;
    auto bytes() const noexcept -> std::string_view;
    auto borrow() const noexcept -> ExecutionText;
    // Callers account for produced bytes before mutation; input must be disjoint.
    auto append(std::string_view bytes) noexcept -> void;
    auto transfer() noexcept -> void;

private:
    std::shared_ptr<ExecutionTextStorage> storage;
};

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
    ExecutionValue(const ExecutionValue&) = delete;
    ExecutionValue(ExecutionValue&&) = default;
    auto operator=(const ExecutionValue&) -> ExecutionValue& = delete;
    auto operator=(ExecutionValue&&) -> ExecutionValue& = default;
    ~ExecutionValue() = default;
};

auto execution_atom(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<ConstantAtom>;
// Views borrow the retained value or execution owner; mutation ends the borrow.
auto execution_text(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<std::string_view>;

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
