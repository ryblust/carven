module carven:semantic.evaluation.native.request;

import :semantic.semir.structured;
import :semantic.semir.values;
import :support.function_ref;
import std;

enum class ExecutionNativeKind { Function, Method, Field, Conversion };

// A call-local view of the existing SemIR contract; no provider facts are stored.
struct ExecutionNativeRequest final {
    ExecutionNativeKind kind;
    std::string name;
    std::vector<CallableParameter> parameters;
    TypeID result;
    std::vector<TypeID> failures;
    CallableExecutionKind execution;
};

auto execution_native_request(
    const ExecutionValueAccess& values,
    const SemanticExpression& expression,
    FunctionRef<std::optional<TypeID>(ConstructionTypeRef) noexcept> close_type,
    FunctionRef<std::vector<TypeID>(ConstructionFailureRef) noexcept> close_failures
) noexcept -> std::optional<ExecutionNativeRequest>;
