module carven:backend.preparation.body;

import :backend.preparation;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

// ReadBorrow observes an object. ReadPointer presents an address snapshot as
// a const lvalue for Read argument queries. Realization chooses storage at boundaries.
// ReadArgument applies the source parameter policy when saving an argument.
// OperandValue reads a completed value without requiring mutable access.
// Consume requires transferable owned storage when realization saves a value.
// ProjectionPlace forwards the consumer access through a field or element projection.
// WritePlace requires mutable access; merely locating an object does not.
// ConversionSource retains backing with its original native cv/ref capability.
// The checked Read conversion, rather than storage retention, selects its access.
// NativeTake additionally preserves the C++ query contract T&&, including for
// trivial values whose Carven transfer policy otherwise observes const T&.
enum class PreparedUse {
    ReadBorrow,
    ReadArgument,
    ReadPointer,
    OperandValue,
    ProjectionPlace,
    WritePlace,
    ConstPlace,
    ConversionSource,
    Consume,
    NativeTake
};

enum class PreparedDemand { Effects, Value };

struct PreparedOperand final {
    const SemanticExpression* expression;
    PreparedUse use;
    PreparedDemand demand;
};

struct PreparedOperation final {
    const SemanticExpression& operation;
    bool executes_operation;
    bool requires_execution;
    // A pending observation may change across later execution. Stable reads need no protection.
    bool reads_storage;
    bool requires_coroutine_context;
    std::vector<PreparedOperand> operands;
    std::unique_ptr<OperationPreparation> preparation;
};

struct ExpressionSummary final {
    bool executes_operation;
    bool requires_execution;
    bool reads_storage;
    bool requires_coroutine_context;
};

class BodyPreparation final {
public:
    BodyPreparation(
        const SemIRProgram& semantic,
        BodyID body,
        CallableExecutionKind execution
    ) noexcept;

    auto body() const noexcept -> const SemIRBody&;
    auto execution() const noexcept -> CallableExecutionKind;
    // Propagation markers select their operand; all other operations retain identity.
    static auto operation(const SemanticExpression& source) noexcept -> const SemanticExpression&;
    auto prepare(const SemanticExpression& source) const noexcept -> PreparedOperation;
    auto summary(const SemanticExpression& source) const noexcept -> const ExpressionSummary&;
    auto requires_coroutine_context(const SemanticRegion& source) const noexcept -> bool;
    auto children(LifetimeRegionID lifetime) const noexcept -> std::span<const LocalBindingID>;

    auto borrowed_array_elements(const SemRangeLoop& loop) const noexcept
        -> std::span<const SemanticExpression>;

private:
    auto operands(
        const SemanticExpression& source,
        const OperationPreparation* preparation
    ) const noexcept -> std::vector<PreparedOperand>;

    auto operand(const SemanticExpression& source, PreparedUse use) const noexcept
        -> PreparedOperand;

    auto argument(const SemCallArgument& source) const noexcept -> PreparedOperand;
    const SemIRProgram& semantic;
    const SemIRBody& metadata;
    CallableExecutionKind execution_kind;
    std::flat_set<LocalBindingID> unstable;
    std::unordered_map<const SemanticExpression*, ExpressionSummary> summaries;
    std::unordered_map<const SemanticRegion*, bool> region_contexts;
    std::map<LifetimeRegionID, std::vector<LocalBindingID>> child_bindings;
};
