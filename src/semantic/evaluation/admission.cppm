module carven:semantic.evaluation.admission;

import :semantic.semir.constant_access;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

// This is the executor's structural capability query. Value-dependent failures,
// including bounds, invalid pointers, and execution budgets, remain runtime checks.
auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view>;
auto unsupported_execution_statement(const SemanticStatement& source) noexcept
    -> std::optional<std::string_view>;
auto supported_execution_type(
    const ExecutionValueAccess& values,
    ConstructionTypeRef type,
    bool allow_void = false
) noexcept -> bool;
