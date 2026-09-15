module carven:semantic.evaluation.admission;

import :semantic.evaluation.shape;
import :semantic.semir.structured;
import std;

auto supported_execution_type(
    const ExecutionValueAccess& values,
    const ExecutionTypeShapes& shapes,
    ConstructionTypeRef type,
    bool allow_void = false
) noexcept -> bool;
auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view>;
auto unsupported_execution_statement(const SemanticStatement& source) noexcept
    -> std::optional<std::string_view>;

// Draft and published patterns share these executable alternatives.
template<typename Pattern>
auto supported_execution_pattern(const Pattern& pattern) noexcept -> bool {
    return std::holds_alternative<WildcardPattern>(pattern)
        || std::holds_alternative<LiteralPattern>(pattern)
        || std::holds_alternative<BindingPattern>(pattern)
        || std::holds_alternative<OrPattern>(pattern);
}
