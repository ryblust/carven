module carven:semantic.evaluation.admission;

import :semantic.semir.constant_access;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

// Explicit executor limitations. The expression visitor checks implementation
// coverage; value-dependent failures remain runtime checks.
template<typename Operation>
constexpr auto unsupported_execution_operation() noexcept -> std::optional<std::string_view> {
    if constexpr (std::same_as<Operation, SemEnumConstructor>) {
        return "operation is not supported in execution";
    } else {
        return std::nullopt;
    }
}

auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view>;

auto supported_execution_type(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> bool;
