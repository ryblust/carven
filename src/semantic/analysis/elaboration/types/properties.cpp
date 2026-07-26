module carven:semantic.analysis.elaboration.types.properties.impl;

import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.types;
import :semantic.analysis.elaboration.types.relations;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.type;
import :support.visit;
import std;

auto is_opaque_or_error(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool {
    auto& builder = module_analysis.builder();
    const auto& value = builder.type(id).value;
    return std::holds_alternative<HIRForeignTypeValue>(value)
        || std::holds_alternative<HIRErrorTypeValue>(value);
}

auto require_value_type(
    ModuleAnalysis& module_analysis,
    HIRTypeID type,
    Span span,
    ValueTypeRole role
) noexcept -> HIRTypeID {
    const auto& value = module_analysis.builder().type(type).value;
    if (std::holds_alternative<HIRErrorTypeValue>(value)) {
        return type;
    }
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&value);
    if (builtin == nullptr
        || (builtin->kind != HIRBuiltinType::Void && builtin->kind != HIRBuiltinType::EntryArgs)) {
        return type;
    }
    const auto description = [&]() noexcept -> std::string_view {
        switch (role) {
            case ValueTypeRole::ArrayElement:      return "array element";
            case ValueTypeRole::Binding:           return "binding";
            case ValueTypeRole::Constant:          return "constant";
            case ValueTypeRole::EnumPayload:       return "enum payload";
            case ValueTypeRole::FunctionParameter: return "function parameter";
            case ValueTypeRole::MatchSubject:      return "match subject";
            case ValueTypeRole::StructureField:    return "structure field";
        }
        std::unreachable();
    }();
    module_analysis.emit(
        span,
        std::format("{} requires a value type", description),
        DiagnosticCode::TypeValueRequired
    );
    return error_type(module_analysis, span);
}

auto supports_equality(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool {
    return module_analysis.declarations().supports_equality(id);
}

auto is_numeric(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool {
    auto& builder = module_analysis.builder();
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&builder.type(id).value);
    if (builtin == nullptr) {
        return false;
    }
    return builtin_is_numeric(builtin->kind);
}

auto is_integer(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool {
    auto& builder = module_analysis.builder();
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&builder.type(id).value);
    if (builtin == nullptr) {
        return false;
    }
    return builtin_is_integer(builtin->kind);
}

auto integer_constant_fits(
    const ModuleAnalysis& module_analysis,
    HIRIntegerConstant constant,
    HIRTypeID id
) noexcept -> bool {
    const auto* builtin =
        std::get_if<HIRBuiltinTypeValue>(&module_analysis.builder().type(id).value);
    return builtin != nullptr && integer_constant_fits(constant, builtin->kind);
}

auto is_bool(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool {
    auto& builder = module_analysis.builder();
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&builder.type(id).value);
    return builtin != nullptr && builtin->kind == HIRBuiltinType::Bool;
}

auto is_void(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool {
    auto& builder = module_analysis.builder();
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&builder.type(id).value);
    return builtin != nullptr && builtin->kind == HIRBuiltinType::Void;
}

auto compatible(const ModuleAnalysis& module_analysis, HIRTypeID left, HIRTypeID right) noexcept
    -> bool {
    return type_compatible(module_analysis.builder(), left, right);
}

auto defer_callable_compatibility(
    ModuleAnalysis& module_analysis,
    HIRExprID source,
    std::variant<HIRTypeID, HIRExprID> target,
    ProgramOriginID constraint_origin,
    DiagnosticCode code,
    std::string_view message
) noexcept -> void {
    auto& builder = module_analysis.builder();
    const auto callable = [&](HIRTypeID type) noexcept {
        const auto& value = builder.type(type).value;
        return std::holds_alternative<HIRFunctionTypeValue>(value)
            || std::holds_alternative<HIRFunctionRefTypeValue>(value)
            || std::holds_alternative<HIRClosureTypeValue>(value);
    };
    const auto target_type = std::visit(
        Overloaded {
            [](HIRTypeID type) static noexcept { return type; },
            [&](HIRExprID expression) noexcept {
                return expression_type(module_analysis, expression);
            },
        },
        target
    );
    if (!callable(target_type) || !callable(expression_type(module_analysis, source))) {
        return;
    }
    module_analysis.callable_constraints().append({
        .source = source,
        .target = target,
        .origin = constraint_origin,
        .code = code,
        .message = std::string(message),
    });
}
