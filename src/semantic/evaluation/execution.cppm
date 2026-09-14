module carven:semantic.evaluation.execution;

import :diagnostics.code;
import :semantic.evaluation.limits;
import :semantic.evaluation.output;
import :semantic.evaluation.value;
import :semantic.semir.structured;
import std;

struct ConstantExecutionDiagnostic final {
    ProgramOriginID origin;
    DiagnosticCode code;
    std::string message;
    std::vector<ProgramOriginID> calls;
};

// A requested semantic dependency failed and its owner has already diagnosed it.
struct ConstantDependencyFailure final {};

using ConstantCallFailure = std::variant<ConstantExecutionDiagnostic, ConstantDependencyFailure>;

// Failure has already been delivered through the execution context.
struct ConstantExecutionFailure final {};

template<typename Value>
using ConstantExecutionResult = std::expected<Value, ConstantExecutionFailure>;

struct ConstantCallBody final {
    // The context keeps completed bodies stable across nested call requests.
    const StructuredBodyDraft& body;
    std::span<const ConstructionTypeRef> parameter_types;
};

class ConstantExecutionContext {
public:
    virtual ~ConstantExecutionContext() = default;
    virtual auto write(ConstantOutputStream stream, std::string_view bytes) noexcept -> void = 0;
    virtual auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> = 0;
    virtual auto prepare_call(FunctionID function, ProgramOriginID origin) noexcept
        -> std::expected<ConstantCallBody, ConstantCallFailure> = 0;
    virtual auto report(const ConstantExecutionDiagnostic& diagnostic) noexcept -> void = 0;
};

auto execute_constant_root(
    ConstantValueAccess& values,
    ConstantExecutionContext& context,
    const SemanticExpression& expression,
    ConstantExecutionLimits limits = {}
) noexcept -> ConstantExecutionResult<ConstantExecutionValue>;

auto execute_constant_test(
    ConstantValueAccess& values,
    ConstantExecutionContext& context,
    const StructuredBodyDraft& body,
    ConstantExecutionLimits limits = {}
) noexcept -> ConstantExecutionResult<void>;
