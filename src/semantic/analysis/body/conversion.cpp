module carven:semantic.analysis.body.conversion.impl;

import :diagnostics.code;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expression_site;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.operations;
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
    auto* place = std::get_if<PlaceExpression>(&expression.storage);
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
    if (auto* value = std::get_if<SemanticExpression>(&built.storage)) {
        if (access == AccessMode::Write) {
            return std::unexpected(
                fail(span, DiagnosticCode::AccessWriteArgument, "Write requires a place expression")
            );
        }
        return std::move(*value);
    }
    if (auto* place = std::get_if<PlaceExpression>(&built.storage)) {
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
            return active_builder().make_expression(
                type,
                active_builder().lifetime(),
                evaluation_origin,
                SemTake {UniqueIndirect(std::move(source))}
            );
        }
        source.category = SemanticValueCategory::Value;
        source.lifetime = active_builder().lifetime();
        source.origin = evaluation_origin;
        return source;
    }
    std::unreachable();
}

auto BodyElaborator::coerce_to(
    BuiltExpression& expression,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    auto& built = expression;
    if (built.type() == target) {
        return {};
    }
    if (built.type() == ConstructionTypeRef(draft().intern_builtin_type(BuiltinType::String))
        && target == ConstructionTypeRef(draft().intern_builtin_type(BuiltinType::Str))) {
        auto site = BodyExpressionSite(*this);
        auto converted = site.finish_text(
            TextIntrinsic::AsStr,
            draft().intern_builtin_type(BuiltinType::Str),
            std::move(built),
            std::nullopt,
            span
        );
        if (!converted) {
            return std::unexpected(converted.error());
        }
        built = std::move(*converted);
        return {};
    }
    if (const auto element = array_element(draft(), built.type())) {
        if (const auto target_element = slice_element(draft(), target)) {
            auto invariant = require_invariant_storage_type(*element, *target_element, span);
            if (!invariant) {
                return std::unexpected(invariant.error());
            }
            auto site = BodyExpressionSite(*this);
            auto converted =
                site.finish_slice_call(SliceIntrinsic::FromArray, std::move(built), {}, span);
            if (!converted) {
                return std::unexpected(converted.error());
            }
            built = std::move(*converted);
            return {};
        }
    }
    if (pointer_narrows(draft(), built.type(), target)) {
        auto source = consume_value(built, span, AccessMode::Read);
        if (!source) {
            return std::unexpected(source.error());
        }
        auto constant = std::optional<ConstantID>();
        if (source->constant && std::holds_alternative<TypeID>(target)) {
            constant = draft().intern_constant(
                {.type = std::get<TypeID>(target), .value = NullPointerConstant {}}
            );
        }
        built.storage = active_builder().make_expression(
            target,
            active_builder().lifetime(),
            origin(span),
            SemCast {UniqueIndirect(std::move(*source)), CastKind::PointerRead},
            constant
        );
        return {};
    }
    if (is_cpp_type(built.type()) || is_cpp_type(target)) {
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
        built = std::move(*converted);
        return {};
    }
    if (!compatible(built.type(), target)) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeMismatch, "expression has an incompatible type")
        );
    }

    if (slice_element(draft(), target)) {
        // Reinterpreting a slice cannot perform the per-element adaptations
        // available to an owning array copy.
        return require_invariant_storage_type(built.type(), target, span);
    }

    struct ArrayShape final {
        ConstructionTypeRef element;
        std::uint64_t extent;
    };

    const auto array_shape = [&](ConstructionTypeRef type) noexcept -> std::optional<ArrayShape> {
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            const auto canonical = draft().type_copy(*concrete);
            const auto* array = std::get_if<ArrayTypeValue>(&canonical.value);
            return array == nullptr ? std::nullopt
                                    : std::optional(
                                          ArrayShape {
                                              .element = array->element,
                                              .extent = array->extent,
                                          }
                                      );
        }
        const auto construction = draft().construction_type_copy(std::get<TypeTermID>(type));
        const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value);
        return array == nullptr ? std::nullopt
                                : std::optional(
                                      ArrayShape {
                                          .element = array->element,
                                          .extent = array->extent,
                                      }
                                  );
    };
    const auto source_array = array_shape(built.type());
    const auto target_array = array_shape(target);
    if (source_array.has_value() || target_array.has_value()) {
        if (!source_array.has_value()
            || !target_array.has_value()
            || source_array->extent != target_array->extent) {
            invariant_violation("compatible array coercion lost its matching shape");
        }
        auto source = take_built(expression, span);
        auto value = active_builder().make_expression(
            target,
            active_builder().lifetime(),
            origin(span),
            SemArrayAdopt {UniqueIndirect(std::move(source))}
        );
        built.storage = std::move(value);
        return {};
    }
    const auto* target_term = std::get_if<TypeTermID>(&target);
    if (target_term == nullptr) {
        return {};
    }
    const auto construction = draft().construction_type_copy(*target_term);
    const auto* view = std::get_if<ConstructionCallableViewTypeValue>(&construction.value);
    if (view == nullptr) {
        return {};
    }
    auto contract = callable_contract(expression, span);
    if (!contract.has_value()) {
        return std::unexpected(contract.error());
    }
    const auto adoption_origin = expansion(span, ProgramExpansionReason::CallableAdoption);
    draft().require_failure_subset(
        contract->failures,
        view->failures,
        adoption_origin,
        FailureSubsetRequirementKind::CallableAdoption
    );
    auto source = take_built(built, span);
    auto value = active_builder().make_expression(
        target,
        active_builder().lifetime(),
        adoption_origin,
        SemBorrowCallable {.source = UniqueIndirect(std::move(source))}
    );
    built.storage = std::move(value);
    return {};
}

auto BodyElaborator::infer_value_type(BuiltExpression& expression, Span span) noexcept
    -> AnalysisResult<ConstructionTypeRef> {
    const auto& built = expression;
    if (!built.is_function_reference()) {
        return built.type();
    }
    auto contract = callable_contract(expression, span);
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
    auto site = BodyExpressionSite(*this);
    auto checked = require_expression_boolean(site, value, span);
    if (!checked.has_value()) {
        return std::unexpected(checked.error());
    }
    return consume_value(value, span, AccessMode::Read);
}
