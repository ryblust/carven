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
    return pattern.visit([](const auto& value) static noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        return std::same_as<Value, WildcardPattern>
            || std::same_as<Value, LiteralPattern>
            || std::same_as<Value, RangePattern>
            || std::same_as<Value, BindingPattern>
            || std::same_as<Value, OrPattern>
            || std::same_as<Value, EnumCasePattern>
            || std::same_as<Value, TypeConstraintPattern>
            || std::same_as<Value, ElaboratedTypeConstraintPattern>;
    });
}
