module carven:semantic.evaluation.standard;

import :semantic.evaluation.execution;
import :semantic.semir.constant_access;
import :semantic.semir.structured;
import std;

enum class StandardNativeKind { Void, Bool, Signed, Unsigned, String, Str, Vector, Slice, Opaque };

struct StandardNativeType final {
    const void* identity;
    StandardNativeKind kind;
    const StandardNativeType* element;
    std::string_view name;
    std::size_t integer_bits;
};

// This view borrows the typed operation directory; no source-owned signature is duplicated.
struct StandardNativeResult final {
    const StandardNativeType* type;
    bool asynchronous;
    bool by_value;
};

auto standard_native_result(const ExecutionValueAccess& values, const CppQueryType& query) noexcept
    -> std::optional<StandardNativeResult>;

auto standard_native_result(
    const ExecutionValueAccess& values,
    const GenericCppQueryType& query
) noexcept -> std::optional<StandardNativeResult>;

// The same native contracts serve construction-time and published execution.
// An absent result leaves an operation to an external host or compiled C++.
auto standard_execution_call(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const SemanticExpression& expression,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::optional<std::expected<ExecutionHostResult, ExecutionCallFailure>>;

auto standard_execution_supported(
    const ExecutionValueAccess& values,
    const SemanticExpression& expression
) noexcept -> bool;

auto standard_execution_type(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> bool;

auto standard_native_builtin(const StandardNativeType& type) noexcept -> std::optional<BuiltinType>;

auto standard_native_result_matches(
    const ExecutionValueAccess& values,
    const StandardNativeResult& result,
    ConstructionTypeRef type,
    ModuleID context
) noexcept -> bool;
