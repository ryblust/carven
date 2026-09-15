module carven:semantic.evaluation.execution;

import :diagnostics.code;
import :semantic.evaluation.limits;
import :semantic.evaluation.output;
import :semantic.evaluation.operation;
import :semantic.evaluation.value;
import :semantic.semir.structured;
import std;

struct ExecutionDiagnostic final {
    ProgramOriginID origin;
    DiagnosticCode code;
    std::string message;
    std::vector<ProgramOriginID> calls;
};

// A requested semantic dependency failed and its owner has already diagnosed it.
struct ExecutionDependencyFailure final {};

using ExecutionCallFailure = std::variant<ExecutionDiagnostic, ExecutionDependencyFailure>;

// Failure has already been delivered through the execution context.
struct ExecutionFailure final {};

template<typename Value>
using ExecutionResult = std::expected<Value, ExecutionFailure>;

// Borrows the existing operation tree in either construction or published form.
class ExecutionBody final {
public:
    explicit ExecutionBody(const StructuredBodyDraft& body) noexcept;
    explicit ExecutionBody(const SemIRBody& body) noexcept;
    auto region() const noexcept -> const SemanticRegion&;
    auto parameters() const noexcept -> std::span<const LocalBindingID>;
    auto binding_count() const noexcept -> std::size_t;
    auto binding_type(LocalBindingID id) const noexcept -> ConstructionTypeRef;

    template<typename Visitor>
    auto visit_pattern(PatternID id, Visitor visitor) const noexcept {
        return std::visit(
            [&](const auto* value) noexcept {
                if constexpr (std::same_as<
                                  std::remove_cvref_t<decltype(*value)>,
                                  StructuredBodyDraft>) {
                    return visitor(value->patterns.get(id));
                } else {
                    return visitor(value->pattern(id));
                }
            },
            body
        );
    }

private:
    std::variant<const StructuredBodyDraft*, const SemIRBody*> body;
};

enum class ExecutionTraceKind { Statement, Call, Return };

struct ExecutionTraceEvent final {
    ExecutionTraceKind kind;
    ProgramOriginID origin;
    std::optional<FunctionID> function;
    std::size_t depth;
};

struct ExecutionCallBody final {
    // The context keeps completed bodies stable across nested call requests.
    ExecutionBody body;
    std::span<const ConstructionTypeRef> parameter_types;
};

class SemanticExecutionContext {
public:
    virtual ~SemanticExecutionContext() = default;

    virtual auto arithmetic() const noexcept -> IntegerArithmetic;
    virtual auto trace(const ExecutionTraceEvent&) noexcept -> void;
    virtual auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void = 0;
    virtual auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> = 0;
    virtual auto prepare_call(FunctionID function, ProgramOriginID origin) noexcept
        -> std::expected<ExecutionCallBody, ExecutionCallFailure> = 0;
    virtual auto report(const ExecutionDiagnostic& diagnostic) noexcept -> void = 0;
};

auto execute_constant_root(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const SemanticExpression& expression,
    ExecutionLimits limits = {}
) noexcept -> ExecutionResult<ExecutionValue>;

auto execute_constant_test(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const StructuredBodyDraft& body,
    ExecutionLimits limits = {}
) noexcept -> ExecutionResult<void>;

auto execute_function(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    FunctionID function,
    std::vector<ExecutionValue> arguments,
    ProgramOriginID origin,
    ExecutionLimits limits = {}
) noexcept -> ExecutionResult<ExecutionValue>;
