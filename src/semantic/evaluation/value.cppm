module carven:semantic.evaluation.value;

import :semantic.semir.async;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :support.input;
import std;

// These shared entities keep global-module identity; their definitions must be
// reachable after import std when this partition is imported indirectly.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
extern "C++" {
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/hosting/object.hpp>
}
#pragma clang diagnostic pop

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

// A slice borrows elements of one live array, Vector, or text backing. Copying it
// copies the coordinate, not its storage. A statically empty slice has no backing
// and zero offset and extent; a dynamically empty view retains its original borrow.
struct ExecutionSlice final {
    ConstructionTypeRef type;
    std::optional<ExecutionPlace> backing;
    std::size_t offset;
    std::size_t extent;
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

// An observation owns only the temporary contiguous bytes needed by its consumer.
// Array-backed text is read afresh; observations never become backing storage.
class ExecutionTextObservation final {
public:
    explicit ExecutionTextObservation(std::string_view bytes) noexcept;
    explicit ExecutionTextObservation(std::string bytes) noexcept;
    auto view() const noexcept -> std::string_view;
    auto size() const noexcept -> std::size_t;
    auto operator==(const ExecutionTextObservation& other) const noexcept -> bool;
    auto operator==(std::string_view other) const noexcept -> bool;

private:
    std::variant<std::string_view, std::string> bytes;
};

// Computed text shares immutable content; views borrow String storage or byte coordinates.
class ExecutionText final {
public:
    explicit ExecutionText(std::string bytes) noexcept;
    explicit ExecutionText(ExecutionSlice bytes) noexcept;
    // The reader's spelling storage must outlive every handle and byte observation.
    static auto retained(std::string_view bytes) noexcept -> ExecutionText;
    auto borrow() const noexcept -> ExecutionText;
    auto is_borrowed() const noexcept -> bool;
    auto size(const ExecutionMemory* memory) const noexcept -> std::optional<std::size_t>;
    auto alive(const ExecutionMemory* memory) const noexcept -> bool;
    auto slice() const noexcept -> const ExecutionSlice*;

    auto observe(
        const ConstantValueReader& values,
        const ExecutionMemory* memory,
        std::size_t offset = 0uz,
        std::size_t count = std::string_view::npos
    ) const noexcept -> std::optional<ExecutionTextObservation>;

private:
    explicit ExecutionText(std::shared_ptr<const ExecutionTextStorage> storage) noexcept;
    explicit ExecutionText(std::weak_ptr<const ExecutionTextStorage> storage) noexcept;
    auto lock() const noexcept -> std::shared_ptr<const ExecutionTextStorage>;

    std::variant<
        std::shared_ptr<const ExecutionTextStorage>,
        std::weak_ptr<const ExecutionTextStorage>,
        ExecutionSlice>
        storage;

    friend class ExecutionOwnedText;
    friend class ExecutionMemory;
};

struct ExecutionChars final {
    ExecutionText text;
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
    auto into_utf8() && noexcept -> std::string;
    auto borrow() const noexcept -> ExecutionText;
    // Callers account for produced bytes before mutation; input must be disjoint.
    auto append(std::string_view bytes) noexcept -> void;
    auto transfer() noexcept -> void;

private:
    std::shared_ptr<ExecutionTextStorage> storage;
};

struct ExecutionPointer final {
    ConstructionTypeRef type;
    std::optional<ExecutionPlace> target;
};

// An internal aggregate field can alias existing execution storage without owning it.
struct ExecutionReference final {
    ConstructionTypeRef type;
    ExecutionPlace target;
};

struct ExecutionFunction final {
    ConstructionTypeRef type;
    // Named functions are stateless; callable objects borrow their existing storage.
    std::variant<CallableID, ExecutionPlace> target;
};

// A host reference transports native identity and optional shared native ownership.
// It contains no execution storage or source-dereferenceable address; adapters resolve it.
struct ExecutionHostObject final {
    ConstructionTypeRef type;
    carven::runtime::hosting::ObjectRef<void> reference;
    bool read_borrows;

    ExecutionHostObject(
        ConstructionTypeRef type,
        carven::runtime::hosting::ObjectRef<void> reference,
        bool read_borrows
    ) noexcept;

    ExecutionHostObject(const ExecutionHostObject& other) noexcept;
    ExecutionHostObject(ExecutionHostObject&&) noexcept = default;
    auto operator=(const ExecutionHostObject& other) noexcept -> ExecutionHostObject&;
    auto operator=(ExecutionHostObject&&) noexcept -> ExecutionHostObject& = default;
};

struct ExecutionValue;

// Constructed aggregates recursively own their storage-bearing children.
// Retained scalars and borrowed views keep their existing representations.
struct ExecutionAggregateValue final {
    ConstructionTypeRef type;
    std::vector<ExecutionValue> elements;
    // Logical capacity for growable owners; host vector allocation is not observable.
    std::size_t capacity;
};

struct ExecutionBytes final {
    ConstructionTypeRef type;
    TypeID element;
    carven::runtime::Vector<std::uint8_t> bytes;
};

struct ExecutionEnumValue final {
    TypeID type;
    EnumCaseID enum_case;
    std::vector<ExecutionValue> payload;
};

class ExecutionExternalOperation;
struct ExecutionColdOperation;

// IDs borrow retained inputs; newly computed facts and text remain execution-local.
struct ExecutionValue final : std::variant<
                                  ConstantID,
                                  ConstantAtom,
                                  ExecutionText,
                                  ExecutionChars,
                                  ExecutionOwnedText,
                                  ExecutionVoid,
                                  ExecutionPointer,
                                  ExecutionReference,
                                  ExecutionSlice,
                                  ExecutionFunction,
                                  ExecutionHostObject,
                                  std::unique_ptr<ExecutionColdOperation>,
                                  ExecutionAggregateValue,
                                  ExecutionBytes,
                                  ExecutionEnumValue> {
    using variant::variant;
    using variant::operator=;
    ExecutionValue(const ExecutionValue&) = delete;
    ExecutionValue(ExecutionValue&&) = default;
    auto operator=(const ExecutionValue&) -> ExecutionValue& = delete;
    auto operator=(ExecutionValue&&) -> ExecutionValue& = default;
    ~ExecutionValue() = default;
};

// Cold arguments are acquired once in the same execution memory domain.
// Ownership is affine; the descriptor executes only when consumed by await.
enum class ExecutionSelectionResult { Indexed, Direct };

struct ExecutionCompositionAction final {
    AsyncCompositionPolicy policy;
    ConstructionTypeRef success;
    ExecutionSelectionResult selection;
};

struct ExecutionTimeoutAction final {
    TypeID timeout_failure;
    TypeID range_failure;
};

struct ExecutionInputAction final {
    HostInputMode mode;
    std::size_t limit;
};

struct ExecutionTimerAction final {
    std::int64_t deadline;
    std::optional<TypeID> failure;
};

struct ExecutionColdOperation final {
    using Action = std::variant<
        CallableID,
        AsyncIntrinsic,
        ExecutionCompositionAction,
        ExecutionTimeoutAction,
        ExecutionInputAction,
        ExecutionTimerAction,
        std::unique_ptr<ExecutionExternalOperation>>;

    ExecutionColdOperation(
        ConstructionTypeRef type,
        Action action,
        std::vector<std::variant<ExecutionValue, ExecutionPlace>> arguments
    ) noexcept;

    ExecutionColdOperation(ExecutionColdOperation&&) noexcept;
    ~ExecutionColdOperation() noexcept;

    ConstructionTypeRef type;
    Action action;
    std::vector<std::variant<ExecutionValue, ExecutionPlace>> arguments;
};

auto execution_atom(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<ConstantAtom>;

auto execution_text(
    const ConstantValueReader& values,
    const ExecutionValue& value,
    const ExecutionMemory* memory = nullptr
) noexcept -> std::optional<ExecutionTextObservation>;

enum class ExecutionComparisonFailure { StepLimit, ExpiredText, UnknownAddress, Unsupported };

// Memory supplies address relations for pointer values; fact-only equality needs no storage.
auto execution_equal(
    const ExecutionValueAccess& values,
    const ExecutionValue& left,
    const ExecutionValue& right,
    std::size_t& steps,
    std::size_t maximum_steps,
    const ExecutionMemory* memory = nullptr
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

auto execution_slice_view(
    const ExecutionSlice& slice,
    const ExecutionMemory* memory = nullptr
) noexcept -> std::optional<ExecutionSequenceView>;

auto transfer_owned_text(ExecutionValue& value) noexcept -> void;

// A stable read view over either retained children or execution-owned slots.
struct ExecutionCompoundView final {
    auto size() const noexcept -> std::size_t;
    ConstructionTypeRef type;
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
