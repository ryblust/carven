module carven:semantic.analysis.body.stmt.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.constant.literal;
import :semantic.analysis.constant.root;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types.display;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.simd;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::initializer_value(
    const ASTVariableDecl& source,
    std::optional<ConstructionTypeRef> declared
) noexcept -> AnalysisTask<SemanticExpression> {
    const auto span = ast.expression(*source.initializer).span;
    auto initializer = (co_await expression(*source.initializer, declared));
    if (!initializer.has_value()) {
        co_return std::unexpected(initializer.error());
    }
    if (declared.has_value()) {
        auto coerced = coerce_to(*initializer, *declared, span);
        if (!coerced.has_value()) {
            co_return std::unexpected(coerced.error());
        }
    } else {
        auto inferred = infer_value_type(*initializer, span);
        if (!inferred.has_value()) {
            co_return std::unexpected(inferred.error());
        }
    }
    co_return consume_value(*initializer, span, AccessMode::Read);
}

// Outside the static stage a constant reads static sources only. Inside it
// every value is one, and the constant freezes what execution produced.
auto BodyElaborator::constant_initializer(
    const ASTVariableDecl& source,
    std::optional<ConstructionTypeRef> declared
) noexcept -> AnalysisTask<SemanticExpression> {
    if (static_stage()) {
        // A failure that leaves the initializer is reported when it executes.
        failure_contexts.push_back({draft().add_empty_failure_term(), false});
        auto value = co_await initializer_value(source, declared);
        failure_contexts.pop_back();
        co_return value;
    }
    auto scope = BodyExprSite(*this);
    auto result = co_await build_static_expression(
        draft(),
        source_module_id,
        ast,
        scope,
        *source.initializer,
        declared
    );
    if (result) {
        co_return std::move(*result);
    }
    if (const auto* diagnostic = std::get_if<AnalysisFailure>(&result.error())) {
        co_return std::unexpected(*diagnostic);
    }
    co_return std::unexpected(fail(
        source.span,
        DiagnosticCode::ConstInitializer,
        "constant initializer requires admitted static operands"
    ));
}

auto BodyElaborator::variable_statement(const ASTVariableDecl& source) noexcept
    -> AnalysisTask<void> {
    if (!source.initializer.has_value()) {
        co_return std::unexpected(fail(
            source.span,
            DiagnosticCode::ConstInitializer,
            "every binding declaration requires an initializer"
        ));
    }
    auto declared = std::optional<ConstructionTypeRef>();
    if (source.type.has_value()) {
        auto resolved = (co_await resolve_type(*source.type));
        if (!resolved.has_value()) {
            co_return std::unexpected(resolved.error());
        }
        declared = *resolved;
    }
    const auto* named = std::get_if<ASTNamedBindingTarget>(&source.target);
    if (source.kind == ASTBindingKind::Const) {
        auto result = co_await constant_initializer(source, declared);
        if (!result) {
            co_return std::unexpected(result.error());
        }
        const auto type = constant_initializer_type(draft(), result->type.construction());
        if (declared && !compatible(type, *declared)) {
            co_return std::unexpected(fail(
                source.span,
                DiagnosticCode::TypeMismatch,
                "frozen constant differs from its declared type"
            ));
        }
        const auto name = named == nullptr ? std::string("_") : spelling(named->name_span);
        const auto storage = body_builder.add_owner_binding(
            draft().intern_spelling(name),
            type,
            frames.back().lifetime,
            false,
            origin(binding_target_span(source.target))
        );
        auto initializer = OwnedSemanticExpression(std::move(*result));
        batch->static_roots.emplace(
            storage.binding,
            BodyBatchElaborator::StaticRoot {.initializer = *initializer, .value = std::nullopt}
        );
        append_statement(
            SemStaticBinding {.binding = storage.binding, .initializer = std::move(initializer)},
            origin(source.span)
        );
        if (named == nullptr) {
            co_return {};
        }
        co_return bind_local(
            named->name_span,
            BodyLocalStorage {
                .storage = storage,
                .type = type,
                .used = false,
                .takeable = false,
                .static_source = true,
                .role = BodyLocalRole::Local,
                .unused_candidate = std::nullopt,
            },
            DiagnosticCode::NameDuplicateLocal
        );
    }
    auto value = co_await initializer_value(source, declared);
    if (!value.has_value()) {
        co_return std::unexpected(value.error());
    }
    // Keep explicit type selection for the Clang 23 coroutine workaround.
    // Do not replace with value_or; see decl/constant.cpp.
    const auto binding_type = declared ? *declared : value->type.construction();
    const auto name = named == nullptr ? std::string("_") : spelling(named->name_span);
    const auto writable = source.kind == ASTBindingKind::Var;
    const auto storage = body_builder.add_owner_binding(
        draft().intern_spelling(name),
        binding_type,
        frames.back().lifetime,
        writable,
        origin(binding_target_span(source.target))
    );
    body_builder.remember_initializer(storage.binding, *value);
    append_statement(
        SemInitialize {.binding = storage.binding, .initializer = std::move(*value)},
        origin(source.span)
    );
    if (named == nullptr) {
        co_return {};
    }
    co_return bind_local(
        named->name_span,
        BodyLocalStorage {
            .storage = storage,
            .type = binding_type,
            .used = false,
            .takeable = true,
            .static_source = false,
            .role = BodyLocalRole::Local,
            .unused_candidate = std::nullopt,
        },
        DiagnosticCode::NameDuplicateLocal
    );
}

auto BodyElaborator::assignment_statement(const ASTAssignment& source) noexcept
    -> AnalysisTask<void> {
    auto target_expression = (co_await expression(source.target));
    if (!target_expression.has_value()) {
        co_return std::unexpected(target_expression.error());
    }
    auto target = consume_place(*target_expression, ast.expression(source.target).span);
    if (!target.has_value()) {
        co_return std::unexpected(target.error());
    }
    if (source.op == ASTAssignmentOperator::Assign) {
        auto right = (co_await expression(source.value, target->expression.type.construction()));
        if (!right.has_value()) {
            co_return std::unexpected(right.error());
        }
        if (!is_cpp_type(target->expression.type.construction())) {
            auto coerced = coerce_to(
                *right,
                target->expression.type.construction(),
                ast.expression(source.value).span
            );
            if (!coerced.has_value()) {
                co_return std::unexpected(coerced.error());
            }
        }
        auto value = consume_value(*right, ast.expression(source.value).span, AccessMode::Read);
        if (!value.has_value()) {
            co_return std::unexpected(value.error());
        }
        append_statement(
            SemAssign {std::move(target->expression), std::nullopt, std::move(*value)},
            origin(source.span)
        );
        co_return {};
    }
    const auto target_type = target->expression.type.construction();
    const auto layout = [&]() noexcept -> std::optional<SIMDLayout> {
        const auto* id = std::get_if<TypeID>(&target_type);
        if (!id) {
            return std::nullopt;
        }
        const auto canonical = draft().type_copy(*id);
        const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
        return builtin ? simd_layout(builtin->kind) : std::nullopt;
    }();
    const auto operand_type =
        layout && target_type == ConstructionTypeRef(draft().builtin_type(layout->vector))
        ? ConstructionTypeRef(draft().builtin_type(layout->element))
        : target_type;
    auto right = (co_await expression(source.value, operand_type));
    if (!right.has_value()) {
        co_return std::unexpected(right.error());
    }
    auto right_value = consume_value(*right, ast.expression(source.value).span, AccessMode::Read);
    if (!right_value.has_value()) {
        co_return std::unexpected(right_value.error());
    }
    const auto operation = [&]() noexcept {
        switch (source.op) {
            case ASTAssignmentOperator::Add:        return BinaryOperator::Add;
            case ASTAssignmentOperator::Subtract:   return BinaryOperator::Subtract;
            case ASTAssignmentOperator::Multiply:   return BinaryOperator::Multiply;
            case ASTAssignmentOperator::Divide:     return BinaryOperator::Divide;
            case ASTAssignmentOperator::Remainder:  return BinaryOperator::Remainder;
            case ASTAssignmentOperator::BitwiseAnd: return BinaryOperator::BitwiseAnd;
            case ASTAssignmentOperator::BitwiseOr:  return BinaryOperator::BitwiseOr;
            case ASTAssignmentOperator::BitwiseXor: return BinaryOperator::BitwiseXor;
            case ASTAssignmentOperator::LeftShift:  return BinaryOperator::LeftShift;
            case ASTAssignmentOperator::RightShift: return BinaryOperator::RightShift;
            case ASTAssignmentOperator::Assign:     break;
        }
        std::unreachable();
    }();
    auto decision = decide_binary_operator(
        draft(),
        operation,
        target->expression.type.construction(),
        right_value->type.construction(),
        compatible(target->expression.type.construction(), right_value->type.construction()),
        type_supports_equality(draft(), target->expression.type.construction())
    );
    if (!is_cpp_type(target->expression.type.construction())
        && !is_cpp_type(right_value->type.construction())
        && (!decision.has_value()
            || (*decision != OperatorResult::Operand
                && ConstructionTypeRef(draft().builtin_type(*operator_result_builtin(*decision)))
                    != target_type))) {
        co_return std::unexpected(fail(
            source.operator_span,
            decision.has_value() ? DiagnosticCode::TypeBinary : decision.error().code,
            decision.has_value()
                ? "compound assignment has no value result"
                : std::format(
                      "{}: '{}' and '{}'",
                      decision.error().message,
                      type_display_name(draft(), target->expression.type.construction()),
                      type_display_name(draft(), right_value->type.construction())
                  )
        ));
    }
    if (layout
        && right_value->type.construction()
            == ConstructionTypeRef(draft().builtin_type(layout->element))) {
        auto operands = std::vector<SemCallArgument>();
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*right_value)});
        right_value = active_builder().make_expression(
            target_type,
            active_builder().lifetime(),
            origin(source.operator_span),
            SemIntrinsic {.operation = SIMDIntrinsic::Splat, .operands = std::move(operands)}
        );
    }
    append_statement(
        SemAssign {std::move(target->expression), operation, std::move(*right_value)},
        origin(source.span)
    );
    co_return {};
}

auto BodyElaborator::update_statement(const ASTUpdate& source) noexcept -> AnalysisTask<void> {
    auto target_expression = (co_await expression(source.target));
    if (!target_expression.has_value()) {
        co_return std::unexpected(target_expression.error());
    }
    auto target = consume_place(*target_expression, ast.expression(source.target).span);
    if (!target.has_value()) {
        co_return std::unexpected(target.error());
    }
    if (is_cpp_type(target->expression.type.construction())) {
        auto operands = std::vector<SemCallArgument>();
        operands.push_back(
            {.access = AccessMode::Write, .expression = std::move(target->expression)}
        );
        auto updated = cpp_expression(
            CppUpdateOperation {.increment = source.op == ASTUpdateOperator::Increment},
            std::move(operands),
            source.span,
            draft().builtin_type(BuiltinType::Void)
        );
        if (!updated.has_value()) {
            co_return std::unexpected(updated.error());
        }
        append_statement(
            SemExpressionStatement {take_built(*updated, source.span)},
            origin(source.span)
        );
        co_return {};
    }
    const auto* concrete = std::get_if<TypeID>(&target->expression.type.construction());
    if (concrete == nullptr) {
        co_return std::unexpected(fail(
            source.span,
            DiagnosticCode::TypeUpdateInteger,
            "update requires an integer target"
        ));
    }
    const auto canonical = draft().type_copy(*concrete);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
        co_return std::unexpected(fail(
            source.span,
            DiagnosticCode::TypeUpdateInteger,
            "update requires an integer target"
        ));
    }
    auto one = normalize_literal(
        draft(),
        ASTLiteral {
            .span = source.operator_span,
            .value =
                IntegerLiteralValue {
                    .value_span = source.operator_span,
                    .magnitude = 1u,
                    .base = IntegerBase::Decimal,
                    .suffix = NumericSuffix::None,
                    .conversion = NumericConversion::Exact,
                },
        },
        target->expression.type.construction()
    );
    if (!one.has_value()) {
        invariant_violation("integer update literal could not be normalized");
    }
    auto right = active_builder().make_expression(
        one->type,
        active_builder().lifetime(),
        origin(source.operator_span),
        SemConstant {.constant = draft().intern_constant(*one)}
    );
    append_statement(
        SemAssign {
            std::move(target->expression),
            source.op == ASTUpdateOperator::Increment ? BinaryOperator::Add
                                                      : BinaryOperator::Subtract,
            std::move(right)
        },
        origin(source.span)
    );
    co_return {};
}

auto BodyElaborator::return_statement(
    std::optional<ASTExprID> operand,
    Span span,
    Span keyword_span,
    bool implicit
) noexcept -> AnalysisTask<void> {
    if (!transfer_boundaries.empty()) {
        static_cast<void>(fail(
            keyword_span,
            DiagnosticCode::FlowTransferBoundary,
            std::format("return cannot cross {}", transfer_boundaries.back().construct)
        ));
        reachable = false;
        co_return {};
    }
    auto value = std::optional<SemanticExpression>();
    if (operand.has_value()) {
        auto built = (co_await expression(*operand, infer_result ? std::nullopt : result_type));
        if (!built.has_value()) {
            co_return std::unexpected(built.error());
        }
        const auto completes = !does_not_complete(*built);
        if (result_type.has_value()
            && is_void_type(draft(), *result_type)
            && !is_void_type(draft(), built->type())
            && !does_not_complete(*built)) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::TypeReturnValue,
                "void callable requires a void return operand"
            ));
        }
        if (!does_not_complete(*built)) {
            if (!result_type.has_value()) {
                auto inferred = infer_value_type(*built, ast.expression(*operand).span);
                if (!inferred.has_value()) {
                    co_return std::unexpected(inferred.error());
                }
                result_type = *inferred;
            } else {
                if (infer_result) {
                    auto inferred = infer_value_type(*built, ast.expression(*operand).span);
                    if (!inferred) {
                        co_return std::unexpected(inferred.error());
                    }
                    if ((is_cpp_type(*inferred) || is_cpp_type(*result_type))
                        && *inferred != *result_type) {
                        co_return std::unexpected(fail(
                            ast.expression(*operand).span,
                            DiagnosticCode::TypeMismatch,
                            "inferred native return types differ; specify a result type"
                        ));
                    }
                    auto compatible = require_invariant_type(
                        *inferred,
                        *result_type,
                        ast.expression(*operand).span
                    );
                    if (!compatible) {
                        co_return std::unexpected(compatible.error());
                    }
                }
                auto coerced = coerce_to(*built, *result_type, ast.expression(*operand).span);
                if (!coerced.has_value()) {
                    co_return std::unexpected(coerced.error());
                }
            }
        }
        if (is_void_type(draft(), built->type())) {
            auto consumed = consume_pending(*built, ast.expression(*operand).span);
            if (!consumed.has_value()) {
                co_return std::unexpected(consumed.error());
            }
            value = take_built(*built, ast.expression(*operand).span);
        } else {
            auto consumed = consume_value(*built, ast.expression(*operand).span, AccessMode::Read);
            if (!consumed.has_value()) {
                co_return std::unexpected(consumed.error());
            }
            value = std::move(*consumed);
        }
        if (!completes) {
            append_statement(SemExpressionStatement {std::move(*value)}, origin(span));
            reachable = false;
            co_return {};
        }
    } else {
        if (!result_type.has_value()) {
            result_type = draft().builtin_type(BuiltinType::Void);
        } else if (!is_void_type(draft(), *result_type)) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::TypeMissingReturnValue,
                "value-returning callable must return a value"
            ));
        }
    }
    append_statement(
        SemReturn {value.has_value() ? std::optional(std::move(*value)) : std::nullopt},
        implicit ? expansion(keyword_span, ProgramExpansionReason::SyntheticControl) : origin(span)
    );
    reachable = false;
    co_return {};
}

auto BodyElaborator::transfer_statement(const ASTControlTransfer& source) noexcept
    -> AnalysisTask<void> {
    switch (source.kind) {
        case ASTControlTransferKind::Return: {
            co_return (
                co_await return_statement(source.value, source.span, source.keyword_span, false)
            );
        }
        case ASTControlTransferKind::Break: {
            if (loops.empty()) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowBreakOutsideLoop,
                    "break is only valid inside a loop"
                ));
            }
            if (!transfer_boundaries.empty()
                && loops.size() <= transfer_boundaries.back().loop_depth) {
                static_cast<void>(fail(
                    source.keyword_span,
                    DiagnosticCode::FlowTransferBoundary,
                    std::format("break cannot cross {}", transfer_boundaries.back().construct)
                ));
                reachable = false;
                co_return {};
            }
            if (source.value.has_value()) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowTransferBoundary,
                    "break cannot carry a value"
                ));
            }
            loops.back().has_break |= reachable && reference_path_reachable;
            append_statement(SemBreak {}, origin(source.span));
            reachable = false;
            co_return {};
        }
        case ASTControlTransferKind::Continue: {
            if (loops.empty()) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowContinueOutsideLoop,
                    "continue is only valid inside a loop"
                ));
            }
            if (!transfer_boundaries.empty()
                && loops.size() <= transfer_boundaries.back().loop_depth) {
                static_cast<void>(fail(
                    source.keyword_span,
                    DiagnosticCode::FlowTransferBoundary,
                    std::format("continue cannot cross {}", transfer_boundaries.back().construct)
                ));
                reachable = false;
                co_return {};
            }
            if (source.value.has_value()) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowTransferBoundary,
                    "continue cannot carry a value"
                ));
            }
            loops.back().has_continue |= reachable && reference_path_reachable;
            append_statement(SemContinue {}, origin(source.span));
            reachable = false;
            co_return {};
        }
        case ASTControlTransferKind::Throw: {
            if (!source.value.has_value()) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectThrowType,
                    "throw requires a payload value"
                ));
            }
            auto payload = (co_await expression(*source.value));
            if (!payload.has_value()) {
                co_return std::unexpected(payload.error());
            }
            auto value =
                consume_value(*payload, ast.expression(*source.value).span, AccessMode::Read);
            if (!value.has_value()) {
                co_return std::unexpected(value.error());
            }
            const auto* failure_type = std::get_if<TypeID>(&value->type.construction());
            if (failure_type == nullptr || !is_failure_payload_type(draft(), *failure_type)) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectThrowType,
                    "throw payload must have a concrete nominal value type"
                ));
            }
            const auto& target = failure_context_for_current_path();
            const auto throw_origin = origin(source.span);
            draft().add_thrown_failure_member(target.term, *failure_type, throw_origin);
            append_statement(SemThrow {std::move(*value), *failure_type}, throw_origin);
            reachable = false;
            co_return {};
        }
        case ASTControlTransferKind::Rethrow: {
            if (catches.empty()) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectRethrowContext,
                    "rethrow is only valid inside a catch arm"
                ));
            }
            if (source.value.has_value()) {
                co_return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectRethrowContext,
                    "rethrow cannot carry a replacement payload"
                ));
            }
            const auto& caught = catches.back();
            const auto& target = failure_context_for_current_path();
            draft().add_failure_contribution(target.term, caught.failures);
            append_statement(SemRethrow {}, origin(source.span));
            reachable = false;
            co_return {};
        }
    }
    std::unreachable();
}
