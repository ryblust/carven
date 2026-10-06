module carven:semantic.analysis.body.conversion.impl;

import :diagnostics.code;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.expr.conversion;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.operations;
import :semantic.analysis.types.display;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import std;

auto BodyElaborator::consume_place(BuiltExpression& expression, Span span) noexcept
    -> AnalysisResult<PlaceExpression> {
    auto consumed = consume_pending(expression, span);
    if (!consumed.has_value()) {
        return std::unexpected(consumed.error());
    }
    auto* place = std::get_if<PlaceExpression>(&*expression.storage);
    if (place == nullptr) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::AccessNotAssignable,
            "expression does not denote an assignable place"
        ));
    }
    place->expression.origin = origin(span);
    return std::move(*place);
}

auto BodyElaborator::consume_value(
    BuiltExpression& expression,
    Span span,
    AccessMode access
) noexcept -> AnalysisResult<SemanticExpression> {
    auto& built = expression;
    if (access == AccessMode::Take && !built.takeable) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::AccessTakeOperand,
            "this binding cannot be used as a Take source"
        ));
    }
    auto consumed = consume_pending(expression, span);
    if (!consumed.has_value()) {
        return std::unexpected(consumed.error());
    }
    if (is_void_type(draft(), built.type())) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeValueRequired,
            "void expression cannot be used as a value"
        ));
    }
    if (built.is_function_reference()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeValueRequired,
            "function declaration requires a callable-value context"
        ));
    }
    if (auto* value = std::get_if<SemanticExpression>(&*built.storage)) {
        if (access == AccessMode::Write) {
            return std::unexpected(
                fail(span, DiagnosticCode::AccessWriteArgument, "Write requires a place expression")
            );
        }
        return std::move(*value);
    }
    if (auto* place = std::get_if<PlaceExpression>(&*built.storage)) {
        if (access == AccessMode::Write) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::AccessWriteArgument,
                "Write argument must remain a place"
            ));
        }
        auto source = std::move(place->expression);
        const auto evaluation_origin = expansion(span, ProgramExpansionReason::EvaluationTemporary);
        if (access == AccessMode::Take) {
            const auto type = source.type.construction();
            const auto operation_reachable = built.completes && source.operation_reachable;
            auto value = active_builder().make_expression(
                type,
                active_builder().lifetime(),
                evaluation_origin,
                SemTake {UniqueIndirect(std::move(source))}
            );
            value.operation_reachable = operation_reachable;
            return value;
        }
        source.category = SemanticValueCategory::Value;
        source.lifetime = active_builder().lifetime();
        source.origin = evaluation_origin;
        return source;
    }
    std::unreachable();
}

auto BodyElaborator::require_invariant_type(
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    if (is_cpp_type(source) || is_cpp_type(target)) {
        return {};
    }

    if (!constrain_invariant_type(draft(), source, target, origin(span))) {
        return std::unexpected(type_mismatch(source, target, span));
    }
    return {};
}

auto BodyElaborator::type_mismatch(
    ConstructionTypeRef actual,
    ConstructionTypeRef expected,
    Span span
) noexcept -> AnalysisFailure {
    return fail(
        span,
        DiagnosticCode::TypeMismatch,
        std::format(
            "expected type '{}', found '{}'",
            type_display_name(draft(), expected),
            type_display_name(draft(), actual)
        )
    );
}

auto BodyElaborator::coerce_to(
    BuiltExpression& expression,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    auto& built = expression;
    // Static parameters are bound at a direct call; a callable value has none.
    if (built.is_function_reference()) {
        const auto contract = callable_contract(built.type(), span);
        if (contract
            && std::ranges::any_of(contract->parameters, [](const auto& parameter) static noexcept {
                   return parameter.stage == ParameterStage::Static;
               })) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::ConstAdmission,
                "function with const parameters requires a direct call; "
                "unbound static inputs cannot form a runtime callable value"
            ));
        }
    }
    auto site = BodyExprSite(*this);
    const auto converted =
        require_body_expression(convert_intrinsic_argument(site, built, target, span));
    if (!converted) {
        return std::unexpected(converted.error());
    }
    if (*converted) {
        return {};
    }
    if (is_cpp_type(built.type()) || is_cpp_type(target)) {
        const auto operation_reachable = built.completes && built.expression().operation_reachable;
        auto value = consume_value(built, span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        auto operands = std::vector<SemCallArgument>();
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
        auto converted = cpp_expression(
            CppConvertOperation {.explicit_cast = false},
            std::move(operands),
            span,
            target
        );
        if (!converted.has_value()) {
            return std::unexpected(converted.error());
        }
        converted->completes = built.completes;
        std::get<SemanticExpression>(*converted->storage).operation_reachable = operation_reachable;
        built = std::move(*converted);
        return {};
    }
    if (!constrain_type_adaptation(draft(), built.type(), target, origin(span))) {
        return std::unexpected(type_mismatch(built.type(), target, span));
    }
    const auto source_array = sequence_shape(draft(), built.type());
    const auto target_array = sequence_shape(draft(), target);
    if ((source_array && source_array->extent) || (target_array && target_array->extent)) {
        if (!source_array || !target_array || source_array->extent != target_array->extent) {
            invariant_violation("compatible array coercion lost its matching shape");
        }
        const auto operation_reachable = built.completes && built.expression().operation_reachable;
        auto source = take_built(expression, span);
        auto value = active_builder().make_expression(
            target,
            active_builder().lifetime(),
            origin(span),
            SemArrayAdopt {UniqueIndirect(std::move(source))}
        );
        value.operation_reachable = operation_reachable;
        *built.storage = std::move(value);
        return {};
    }
    const auto target_is_view = target.visit([&](const auto id) noexcept {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(id)>, TypeID>) {
            return std::holds_alternative<CallableViewTypeValue>(draft().type_copy(id).value);
        } else {
            return std::holds_alternative<ConstructionCallableViewTypeValue>(
                draft().construction_type_copy(id).value
            );
        }
    });
    if (!target_is_view) {
        built.expression().type = BodyType(target);
        return {};
    }
    const auto adoption_origin = expansion(span, ProgramExpansionReason::CallableAdoption);
    const auto operation_reachable = built.completes && built.expression().operation_reachable;
    auto source = take_built(built, span);
    auto value = active_builder().make_expression(
        target,
        active_builder().lifetime(),
        adoption_origin,
        SemBorrowCallable {.source = UniqueIndirect(std::move(source))}
    );
    value.operation_reachable = operation_reachable;
    *built.storage = std::move(value);
    return {};
}

auto BodyElaborator::infer_value_type(BuiltExpression& expression, Span span) noexcept
    -> AnalysisResult<ConstructionTypeRef> {
    const auto& built = expression;
    if (!built.is_function_reference()) {
        return built.type();
    }
    auto contract = callable_contract(expression.type(), span);
    if (!contract.has_value()) {
        return std::unexpected(contract.error());
    }
    const auto type = ConstructionTypeRef {
        draft().append_construction_type(
            ConstructionType {
                .value = ConstructionCallableViewTypeValue {
                    .parameters = contract->parameters,
                    .result = contract->result,
                    .failures = contract->failures,
                },
            }
        ),
    };
    auto coerced = coerce_to(expression, type, span);
    if (!coerced.has_value()) {
        return std::unexpected(coerced.error());
    }
    return type;
}

auto BodyElaborator::require_bool(BuiltExpression& value, Span span) noexcept
    -> AnalysisResult<SemanticExpression> {
    auto site = BodyExprSite(*this);
    auto checked = require_body_expression(require_expression_boolean(site, value, span));
    if (!checked.has_value()) {
        return std::unexpected(checked.error());
    }
    return consume_value(value, span, AccessMode::Read);
}
