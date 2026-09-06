module carven:semantic.analysis.body.conversion.impl;

import :diagnostics.code;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.operations;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {

auto BodyElaborator::as_place(BuiltExpression& expression, Span span) noexcept
    -> AnalysisResult<PlaceHandle> {
    auto consumed = consume_pending(expression, span);
    if (!consumed.has_value()) {
        return std::unexpected(consumed.error());
    }
    const auto* place = std::get_if<PlaceHandle>(&expression.storage);
    if (place == nullptr) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::AccessNotAssignable,
            "expression does not denote an assignable place"
        ));
    }
    return *place;
}

auto BodyElaborator::as_value(BuiltExpression& expression, Span span, AccessMode access) noexcept
    -> AnalysisResult<ExpressionHandle> {
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
    if (is_void_type(draft(), built.type)) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeValueRequired,
            "void expression cannot be used as a value"
        ));
    }
    if (const auto* value = std::get_if<ExpressionHandle>(&built.storage)) {
        if (access == AccessMode::Write) {
            return std::unexpected(
                fail(span, DiagnosticCode::AccessWriteArgument, "Write requires a place expression")
            );
        }
        return *value;
    }
    if (const auto* place = std::get_if<PlaceHandle>(&built.storage)) {
        if (access == AccessMode::Write) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::AccessWriteArgument,
                "Write argument must remain a place"
            ));
        }
        const auto value = active_builder().append_value(
            built.type,
            active_builder().lifetime(),
            access == AccessMode::Take
                ? expression_construction::Input {expression_construction::Take {.place = *place}}
                : expression_construction::Input {expression_construction::Read {.place = *place}},
            expansion(span, ProgramExpansionReason::EvaluationTemporary)
        );
        built.storage = value;
        built.constant.reset();
        return value;
    }
    return std::unexpected(fail(
        span,
        DiagnosticCode::TypeValueRequired,
        std::holds_alternative<DirectCallable>(built.storage)
            ? "function declaration requires a callable-value context"
            : "void expression cannot be used as a value"
    ));
}

auto BodyElaborator::coerce_to(
    BuiltExpression& expression,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    auto& built = expression;
    if (built.type == target) {
        return {};
    }
    if (is_cpp_type(built.type) || is_cpp_type(target)) {
        auto value = as_value(built, span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        auto operands = std::vector<SemCallArgument<ConstructionTypeRef, FailureTermID>>();
        operands.push_back(
            {.access = AccessMode::Read, .expression = active_builder().take_value(*value)}
        );
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
    if (!compatible(built.type, target)) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeMismatch, "expression has an incompatible type")
        );
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
    const auto source_array = array_shape(built.type);
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
            SemArrayAdopt<ConstructionTypeRef, FailureTermID> {UniqueIndirect(std::move(source))}
        );
        built.type = target;
        built.storage = body_builder.add_expression(std::move(value));
        built.constant.reset();
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
    auto backing = std::optional<expression_construction::CallableSource>();
    std::visit(
        Overloaded {
            [&](const DirectCallable& direct) noexcept {
                backing = expression_construction::CallableSource {direct.callable};
            },
            [&](ExpressionHandle value) noexcept {
                backing = expression_construction::CallableSource {value};
            },
            [&](PlaceHandle place) noexcept {
                backing = expression_construction::CallableSource {place};
            },
            [](const NoExpressionValue&) static noexcept {},
        },
        built.storage
    );
    if (!backing.has_value()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeNotCallable,
            "void expression cannot form a callable view"
        ));
    }
    const auto value = active_builder().append_unresolved_callable_borrow(
        target,
        active_builder().lifetime(),
        UnresolvedCallableBorrowOp {
            .backing = *backing,
            .target_type = target,
            .source_failure_term_id = contract->failures,
            .target_failure_term_id = view->failures,
            .loan_lifetime = std::visit(
                Overloaded {
                    [&](CallableID) noexcept { return static_lifetime_id; },
                    [&](ExpressionHandle) noexcept { return active_builder().lifetime(); },
                    [&](PlaceHandle place) noexcept { return body_builder.place_lifetime(place); },
                },
                *backing
            ),
        },
        expansion(span, ProgramExpansionReason::CallableAdoption)
    );
    built.type = target;
    built.storage = value;
    built.constant.reset();
    return {};
}

auto BodyElaborator::infer_value_type(BuiltExpression& expression, Span span) noexcept
    -> AnalysisResult<ConstructionTypeRef> {
    const auto& built = expression;
    if (!std::holds_alternative<DirectCallable>(built.storage)) {
        return built.type;
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
    -> AnalysisResult<ExpressionHandle> {
    const auto boolean = ConstructionTypeRef {
        draft().intern_builtin_type(BuiltinType::Bool),
    };
    if (is_cpp_type(value.type)) {
        auto converted = coerce_to(value, boolean, span);
        if (!converted.has_value()) {
            return std::unexpected(converted.error());
        }
    }
    if (!compatible(value.type, boolean)) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeConditionBool, "condition must have type bool")
        );
    }
    return as_value(value, span, AccessMode::Read);
}

} // namespace body_elaboration
