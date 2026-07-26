module carven:semantic.analysis.elaboration.expressions.aggregate.impl;

import :frontend.ast.expr;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir.expr;
import :semantic.hir.type;
import std;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTArrayExpr& array,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto& value = ast.expression(id);
    auto elements = std::vector<HIRExprID>();
    if (array.element_ids.empty()) {
        module_analysis.emit(
            value.span,
            "cannot infer the element type of an empty array",
            DiagnosticCode::TypeEmptyArray
        );
        return append_expression(
            module_analysis,
            {
                .origin = expression_origin,
                .type = error_type(module_analysis, value.span),
                .constant = std::nullopt,
                .value = HIRArrayExpr {.element_ids = {}},
            }
        );
    }
    const auto contextual_case = [&](ASTExprID element) noexcept {
        const auto& candidate = ast.expression(element).value;
        if (std::holds_alternative<ASTContextualCaseExpr>(candidate)) {
            return true;
        }
        const auto* call = std::get_if<ASTCallExpr>(&candidate);
        return call != nullptr
            && std::holds_alternative<ASTContextualCaseExpr>(ast.expression(call->callee).value);
    };
    const auto anchor = std::ranges::find_if(array.element_ids, [&](ASTExprID element) noexcept {
        return !contextual_case(element);
    });
    auto element_type = std::optional<HIRTypeID>();
    if (anchor == array.element_ids.end()) {
        for (const auto element : array.element_ids) {
            elements.push_back(build_expression(module_analysis, scopes, control, element));
        }
    } else {
        const auto anchor_value = build_expression(module_analysis, scopes, control, *anchor);
        element_type = expression_type(module_analysis, anchor_value);
        for (const auto element : array.element_ids) {
            elements.push_back(
                element == *anchor
                    ? anchor_value
                    : (contextual_case(element)
                           ? build_expected_expression(
                                 module_analysis,
                                 scopes,
                                 control,
                                 element,
                                 *element_type
                             )
                           : build_expression(module_analysis, scopes, control, element))
            );
        }
    }
    for (auto index = 0uz; index < elements.size(); ++index) {
        if (!element_type.has_value()
            || !compatible(
                module_analysis,
                *element_type,
                expression_type(module_analysis, elements[index])
            )) {
            module_analysis.emit(
                ast.expression(array.element_ids[index]).span,
                "array elements have incompatible types",
                DiagnosticCode::TypeArrayElement
            );
        }
    }
    const auto resolved_element_type = require_value_type(
        module_analysis,
        element_type.has_value() ? *element_type : error_type(module_analysis, value.span),
        anchor == array.element_ids.end() ? value.span : ast.expression(*anchor).span,
        ValueTypeRole::ArrayElement
    );
    const auto array_type = builder.intern_type({
        .value = HIRArrayTypeValue {
            .element_type_id = resolved_element_type,
            .extent = static_cast<std::uint64_t>(elements.size()),
        },
    });
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = array_type,
            .constant = std::nullopt,
            .value = HIRArrayExpr {.element_ids = std::move(elements)},
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstructionExpr& construction,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    return build_construction_expression(
        module_analysis,
        scopes,
        control,
        construction,
        expression_origin
    );
}
