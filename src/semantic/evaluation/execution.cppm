module carven:semantic.evaluation.execution;

import :semantic.evaluation.limits;
import :semantic.evaluation.operation;
import :semantic.evaluation.output;
import :semantic.evaluation.value;
import :semantic.semir.structured;
import :support.task;
import std;

// The operation decides whether its report continues, stops this execution
// root, or aborts the whole run. Presentation and diagnostic codes do not decide it.
enum class ExecutionTermination { Continue, StopRoot, Abort };

// The evaluator names causes; static and interpreted adapters select their
// diagnostic codes without changing execution or termination.
enum class ExecutionReason {
    Admission,
    Evaluation,
    Limit,
    Cycle,
    Unavailable,
    Overflow,
    DivideByZero,
    ShiftOutOfRange,
    LiteralRange,
    IndexBounds,
    Assertion,
    Test,
};

struct ExecutionReportField final {
    // Labels name fixed language report fields and borrow static spellings.
    std::string_view label;
    std::string text;
};

// Ordinary execution issues have a cause and a stopping scope. Language reports
// derive their reason, title and continuation behavior from ReportKind.
struct ExecutionIssue final {
    ExecutionReason reason;
    std::string message;
    // A stopping issue is either local to the root or fatal to the run.
    ExecutionTermination termination;
};

struct ExecutionEvent final {
    ProgramOriginID origin;
    std::variant<ExecutionIssue, ReportKind> cause;
    std::vector<ExecutionReportField> fields;
    std::vector<ProgramOriginID> calls;
    auto reason() const noexcept -> ExecutionReason;
    auto message() const noexcept -> std::string_view;
    auto report_kind() const noexcept -> std::optional<ReportKind>;
    auto termination() const noexcept -> ExecutionTermination;
};

// A requested semantic dependency failed and its owner has already diagnosed it.
struct ExecutionDependencyFailure final {};

// The event has already been delivered synchronously. The result owns its cause
// and stop scope, so a consumer never reconstructs control from report history.
struct ExecutionHalt final {
    ExecutionEvent event;
};

// Immutable owned payload survives unwinding and rejected catch attempts without
// unaccounted aggregate copies. Catch bindings obtain their own execution values.
struct ExecutionSourceFailure final {
    TypeID type;
    std::shared_ptr<const ExecutionValue> payload;
    ProgramOriginID origin;
    std::vector<ProgramOriginID> calls;
};

using ExecutionFailure =
    std::variant<ExecutionHalt, ExecutionSourceFailure, ExecutionDependencyFailure>;

// An adapter supplies either a new event for the executor to report or an
// existing execution failure to propagate without delivering it again.
using ExecutionCallFailure = std::variant<ExecutionEvent, ExecutionFailure>;

template<typename Value>
using ExecutionResult = std::expected<Value, ExecutionFailure>;

template<typename Value>
using ExecutionTask = ContinuationTask<ExecutionResult<Value>>;

using ExecutionOperand = std::variant<ExecutionValue, ExecutionPlace>;

// Call adapters can consume an argument as an independent value without
// accessing the executor's slots or retaining its borrowed storage.
class ExecutionArgumentAccess {
public:
    virtual ~ExecutionArgumentAccess() = default;
    virtual auto detach_argument(ExecutionOperand operand, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue> = 0;
};

// Borrows the existing operation tree in either construction or published form.
class ExecutionBody final {
public:
    explicit ExecutionBody(const StructuredBodyDraft& body) noexcept;
    explicit ExecutionBody(const SemIRBody& body) noexcept;
    ExecutionBody(const StructuredBodyDraft& body, const SemanticRegion& region) noexcept;
    ExecutionBody(const SemIRBody& body, const SemanticRegion& region) noexcept;
    auto kind() const noexcept -> BodyKind;
    auto region() const noexcept -> const SemanticRegion&;
    auto parameters() const noexcept -> std::span<const LocalBindingID>;
    auto binding_count() const noexcept -> std::size_t;
    auto binding_type(LocalBindingID id) const noexcept -> ConstructionTypeRef;
    auto binding_access(LocalBindingID id) const noexcept -> AccessMode;
    auto bindings_in(LifetimeRegionID lifetime) const noexcept -> std::vector<std::size_t>;

    template<typename Visitor>
    auto visit_pattern(PatternID id, Visitor&& visitor) const noexcept {
        return body.visit([&](const auto* value) noexcept {
            if constexpr (std::
                              same_as<std::remove_cvref_t<decltype(*value)>, StructuredBodyDraft>) {
                return std::forward<Visitor>(visitor)(value->patterns.get(id));
            } else {
                return std::forward<Visitor>(visitor)(value->pattern(id));
            }
        });
    }

private:
    std::variant<const StructuredBodyDraft*, const SemIRBody*> body;
    const SemanticRegion* selected_region;
};

enum class ExecutionStage { Static, Interpreted };

enum class ExecutionTraceKind { Statement, Call, Return };

struct ExecutionTraceEvent final {
    ExecutionTraceKind kind;
    ProgramOriginID origin;
    std::optional<FunctionID> function;
    std::size_t depth;
};

class SemanticExecutionContext {
public:
    virtual ~SemanticExecutionContext() = default;

    virtual auto stage() const noexcept -> ExecutionStage;
    virtual auto trace(const ExecutionTraceEvent&) noexcept -> void;
    virtual auto enter_block(BlockSource source) noexcept -> void;
    virtual auto leave_block() noexcept -> void;
    virtual auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void = 0;
    virtual auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> = 0;
    // Adapts an evaluated call before looking up its executable body. The
    // default context preserves the callable and every argument.
    virtual auto bind_call(
        CallableID callable,
        std::vector<ExecutionOperand>& arguments,
        ExecutionArgumentAccess& access,
        ProgramOriginID origin
    ) noexcept -> ContinuationTask<std::expected<CallableID, ExecutionCallFailure>>;
    // The constant a static binding or a static argument holds. Only static
    // execution freezes values.
    virtual auto freeze(ExecutionValue value) noexcept -> std::optional<ConstantID>;
    virtual auto prepare_call(CallableID callable, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> = 0;
    virtual auto report(const ExecutionEvent& event) noexcept -> void = 0;
};

// Executes an expression that reads no local binding.
auto execute_static_root(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const SemanticExpression& expression,
    ExecutionLimits limits = static_execution_limits()
) noexcept -> ExecutionTask<ExecutionValue>;

auto execute_body(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    ExecutionBody body,
    ExecutionLimits limits = static_execution_limits()
) noexcept -> ExecutionTask<void>;

// Starts a typed entry function with no language arguments. Calls inside its
// body use their SemCall operands.
auto execute_function(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    CallableID callable,
    ProgramOriginID origin,
    ExecutionLimits limits = static_execution_limits()
) noexcept -> ExecutionTask<ExecutionValue>;
