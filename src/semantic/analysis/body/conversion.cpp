module carven:semantic.analysis.body.conversion.impl;

import :diagnostics.code;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.expr.conversion;
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

auto BodyElaborator::require_invariant_type(
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    if (is_cpp_type(source) || is_cpp_type(target)) {
        return {};
    }

    struct ArrayShape final {
        ConstructionTypeRef element;
        std::uint64_t extent;
    };

    struct CallableViewShape final {
        std::vector<ConstructionCallableParameter> parameters;
        ConstructionTypeRef result;
        FailureTermID failures;
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
    const auto callable_view_shape =
        [&](ConstructionTypeRef type) noexcept -> std::optional<CallableViewShape> {
        if (std::holds_alternative<TypeID>(type)) {
            return std::nullopt;
        }
        const auto construction = draft().construction_type_copy(std::get<TypeTermID>(type));
        const auto* view = std::get_if<ConstructionCallableViewTypeValue>(&construction.value);
        return view == nullptr ? std::nullopt
                               : std::optional(
                                     CallableViewShape {
                                         .parameters = view->parameters,
                                         .result = view->result,
                                         .failures = view->failures,
                                     }
                                 );
    };
    const auto invariant = [&](this auto&& self,
                               ConstructionTypeRef left,
                               ConstructionTypeRef right) noexcept -> bool {
        if (left == right) {
            return true;
        }
        const auto left_slice = slice_element(draft(), left);
        const auto right_slice = slice_element(draft(), right);
        if (left_slice || right_slice) {
            return left_slice && right_slice && self(*left_slice, *right_slice);
        }
        const auto left_array = array_shape(left);
        const auto right_array = array_shape(right);
        if (left_array.has_value() || right_array.has_value()) {
            return left_array.has_value()
                && right_array.has_value()
                && left_array->extent == right_array->extent
                && self(left_array->element, right_array->element);
        }
        const auto left_view = callable_view_shape(left);
        const auto right_view = callable_view_shape(right);
        if (left_view.has_value() || right_view.has_value()) {
            if (!left_view.has_value()
                || !right_view.has_value()
                || left_view->parameters.size() != right_view->parameters.size()
                || !self(left_view->result, right_view->result)) {
                return false;
            }
            for (auto index = 0uz; index < left_view->parameters.size(); ++index) {
                if (left_view->parameters[index].access != right_view->parameters[index].access
                    || !self(
                        left_view->parameters[index].type,
                        right_view->parameters[index].type
                    )) {
                    return false;
                }
            }
            draft().require_equal_failures(left_view->failures, right_view->failures, origin(span));
            return true;
        }
        const auto* left_type = std::get_if<TypeID>(&left);
        const auto* right_type = std::get_if<TypeID>(&right);
        return left_type != nullptr
            && right_type != nullptr
            && draft().type_copy(*left_type) == draft().type_copy(*right_type);
    };
    if (!invariant(source, target)) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeMismatch, "types must match")
        );
    }
    return {};
}

auto BodyElaborator::require_adaptation(
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    if (source == target) {
        return {};
    }
    if (slice_element(draft(), target)) {
        return require_invariant_type(source, target, span);
    }
    if (const auto element = array_element(draft(), target)) {
        const auto source_element = array_element(draft(), source);
        if (!source_element) {
            invariant_violation("compatible array adoption lost its source shape");
        }
        return require_adaptation(*source_element, *element, span);
    }
    const auto* term = std::get_if<TypeTermID>(&target);
    if (term == nullptr) {
        return {};
    }
    const auto construction = draft().construction_type_copy(*term);
    const auto* view = std::get_if<ConstructionCallableViewTypeValue>(&construction.value);
    if (view == nullptr) {
        return {};
    }
    const auto contract = callable_contract(source, span);
    if (!contract) {
        return std::unexpected(contract.error());
    }
    if (auto checked = require_invariant_type(contract->result, view->result, span); !checked) {
        return checked;
    }
    for (auto index = 0uz; index < view->parameters.size(); ++index) {
        if (auto checked = require_invariant_type(
                contract->parameters[index].type,
                view->parameters[index].type,
                span
            );
            !checked) {
            return checked;
        }
    }
    draft().require_failure_subset(
        contract->failures,
        view->failures,
        expansion(span, ProgramExpansionReason::CallableAdoption),
        FailureSubsetRequirementKind::CallableAdoption
    );
    return {};
}

auto BodyElaborator::coerce_to(
    BuiltExpression& expression,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    auto& built = expression;
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

    if (auto checked = require_adaptation(built.type(), target, span); !checked) {
        return checked;
    }
    const auto source_array = sequence_shape(draft(), built.type());
    const auto target_array = sequence_shape(draft(), target);
    if ((source_array && source_array->extent) || (target_array && target_array->extent)) {
        if (!source_array || !target_array || source_array->extent != target_array->extent) {
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
    const auto adoption_origin = expansion(span, ProgramExpansionReason::CallableAdoption);
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
