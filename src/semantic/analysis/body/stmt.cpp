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
import :semantic.analysis.body.expression_site;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.constant;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::variable_statement(const ASTVariableDecl& source) noexcept
    -> AnalysisResult<void> {
    if (!source.initializer.has_value()) {
        return std::unexpected(fail(
            source.span,
            DiagnosticCode::ConstInitializer,
            "every binding declaration requires an initializer"
        ));
    }
    auto declared = std::optional<ConstructionTypeRef>();
    if (source.type.has_value()) {
        auto resolved = resolve_type(*source.type);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        declared = *resolved;
    }
    const auto* named = std::get_if<ASTNamedBindingTarget>(&source.target);
    if (source.kind == ASTBindingKind::Const) {
        auto scope = BodyExpressionSite(*this);
        auto result = evaluate_constant_expression(
            draft(),
            source_module_id,
            ast,
            scope,
            *source.initializer,
            declared
        );
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        const auto* constant = std::get_if<ConstantID>(&*result);
        if (constant == nullptr) {
            return std::unexpected(fail(
                source.span,
                DiagnosticCode::ConstInitializer,
                "const initializer is not a proven Carven constant"
            ));
        }
        if (named == nullptr) {
            return {};
        }
        return bind_local(
            named->name_span,
            BodyLocalStorage {
                .storage = *constant,
                .type = draft().constant_copy(*constant).type,
                .takeable = false,
                .unused_candidate = std::nullopt,
            },
            DiagnosticCode::NameDuplicateLocal
        );
    }
    auto initializer = expression(*source.initializer, declared);
    if (!initializer.has_value()) {
        return std::unexpected(initializer.error());
    }
    if (declared.has_value()) {
        auto coerced = coerce_to(*initializer, *declared, ast.expression(*source.initializer).span);
        if (!coerced.has_value()) {
            return std::unexpected(coerced.error());
        }
    } else {
        auto inferred = infer_value_type(*initializer, ast.expression(*source.initializer).span);
        if (!inferred.has_value()) {
            return std::unexpected(inferred.error());
        }
    }
    auto value =
        consume_value(*initializer, ast.expression(*source.initializer).span, AccessMode::Read);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    const auto binding_type = declared.value_or(value->type.construction());
    const auto name = named == nullptr ? std::string("_") : spelling(named->name_span);
    const auto writable = source.kind == ASTBindingKind::Var;
    const auto storage = body_builder.add_owner_binding(
        draft().intern_spelling(name),
        binding_type,
        frames.back().lifetime,
        writable,
        origin(binding_target_span(source.target))
    );
    append_statement(SemInitialize {storage.binding, std::move(*value)}, source.span);
    if (named == nullptr) {
        return {};
    }
    return bind_local(
        named->name_span,
        BodyLocalStorage {
            .storage = storage,
            .type = binding_type,
            .unused_candidate = std::nullopt,
        },
        DiagnosticCode::NameDuplicateLocal
    );
}

auto BodyElaborator::assignment_statement(const ASTAssignment& source) noexcept
    -> AnalysisResult<void> {
    auto target_expression = expression(source.target);
    if (!target_expression.has_value()) {
        return std::unexpected(target_expression.error());
    }
    auto target = consume_place(*target_expression, ast.expression(source.target).span);
    if (!target.has_value()) {
        return std::unexpected(target.error());
    }
    if (source.op == ASTAssignmentOperator::Assign) {
        auto right = expression(source.value, target->expression.type.construction());
        if (!right.has_value()) {
            return std::unexpected(right.error());
        }
        if (!is_cpp_type(target->expression.type.construction())) {
            auto coerced = coerce_to(
                *right,
                target->expression.type.construction(),
                ast.expression(source.value).span
            );
            if (!coerced.has_value()) {
                return std::unexpected(coerced.error());
            }
        }
        auto value = consume_value(*right, ast.expression(source.value).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        append_statement(
            SemAssign {std::move(target->expression), std::nullopt, std::move(*value)},
            source.span
        );
        return {};
    }
    auto right = expression(source.value, target->expression.type.construction());
    if (!right.has_value()) {
        return std::unexpected(right.error());
    }
    auto right_value = consume_value(*right, ast.expression(source.value).span, AccessMode::Read);
    if (!right_value.has_value()) {
        return std::unexpected(right_value.error());
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
        && (!decision.has_value() || *decision != OperatorResult::Operand)) {
        return std::unexpected(fail(
            source.operator_span,
            decision.has_value() ? DiagnosticCode::TypeBinary : decision.error().code,
            decision.has_value() ? "compound assignment has no value result"
                                 : std::string(decision.error().message)
        ));
    }
    append_statement(
        SemAssign {std::move(target->expression), operation, std::move(*right_value)},
        source.span
    );
    return {};
}

auto BodyElaborator::update_statement(const ASTUpdate& source) noexcept -> AnalysisResult<void> {
    auto target_expression = expression(source.target);
    if (!target_expression.has_value()) {
        return std::unexpected(target_expression.error());
    }
    auto target = consume_place(*target_expression, ast.expression(source.target).span);
    if (!target.has_value()) {
        return std::unexpected(target.error());
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
            draft().intern_builtin_type(BuiltinType::Void)
        );
        if (!updated.has_value()) {
            return std::unexpected(updated.error());
        }
        append_statement(SemExpressionStatement {take_built(*updated, source.span)}, source.span);
        return {};
    }
    const auto* concrete = std::get_if<TypeID>(&target->expression.type.construction());
    if (concrete == nullptr) {
        return std::unexpected(fail(
            source.span,
            DiagnosticCode::TypeUpdateInteger,
            "update requires an integer target"
        ));
    }
    const auto canonical = draft().type_copy(*concrete);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
        return std::unexpected(fail(
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
        source.span
    );
    return {};
}

auto BodyElaborator::test_statement(const ASTTestOperationStmt& source, Span span) noexcept
    -> AnalysisResult<void> {
    if (!is_test) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TestArgumentCount,
            "test operations are only valid in a test body"
        ));
    }
    const auto fail_operation = source.kind == ASTTestOperationKind::Fail;
    const auto minimum = fail_operation ? 0uz : 1uz;
    const auto maximum = fail_operation ? 1uz : 2uz;
    if (source.arguments.size() < minimum || source.arguments.size() > maximum) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TestArgumentCount,
            "test operation has the wrong number of arguments"
        ));
    }
    auto condition = std::optional<SemanticExpression>();
    auto message = std::optional<SemanticExpression>();
    if (!fail_operation) {
        const auto condition_id = source.arguments.front();
        auto built = expression(condition_id, draft().intern_builtin_type(BuiltinType::Bool));
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
        const auto condition_span = ast.expression(condition_id).span;
        const auto boolean = ConstructionTypeRef {
            draft().intern_builtin_type(BuiltinType::Bool),
        };
        if (is_cpp_type(built->type())) {
            auto converted = coerce_to(*built, boolean, condition_span);
            if (!converted.has_value()) {
                return std::unexpected(converted.error());
            }
        }
        if (!compatible(built->type(), boolean)) {
            return std::unexpected(fail(
                condition_span,
                DiagnosticCode::TestConditionType,
                "test condition must have type bool"
            ));
        }
        auto value = consume_value(*built, condition_span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        condition = std::move(*value);
    }
    if (source.arguments.size() == maximum) {
        const auto message_id = source.arguments.back();
        auto built = expression(message_id, draft().intern_builtin_type(BuiltinType::Str));
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
        if (!compatible(built->type(), draft().intern_builtin_type(BuiltinType::Str))) {
            return std::unexpected(fail(
                ast.expression(message_id).span,
                DiagnosticCode::TestMessageType,
                "test message must have type str"
            ));
        }
        auto value = consume_value(*built, ast.expression(message_id).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        message = std::move(*value);
    }
    const auto report_kind = [&]() noexcept {
        switch (source.kind) {
            case ASTTestOperationKind::Check:   return TestReportKind::Check;
            case ASTTestOperationKind::Require: return TestReportKind::Require;
            case ASTTestOperationKind::Fail:    return TestReportKind::Fail;
        }
        std::unreachable();
    }();
    append_statement(
        SemTestReport {
            .kind = report_kind,
            .condition =
                condition.has_value() ? std::optional(std::move(*condition)) : std::nullopt,
            .message = message.has_value() ? std::optional(std::move(*message)) : std::nullopt,
            .condition_source = condition.has_value()
                ? std::optional(
                      draft().intern_spelling(
                          draft().source_slice_copy(
                              source_module_id,
                              ast.expression(source.arguments.front()).span
                          )
                      )
                  )
                : std::nullopt
        },
        span
    );
    if (source.kind != ASTTestOperationKind::Check) {
        regions.back().exits_test = true;
    }
    if (source.kind == ASTTestOperationKind::Fail) {
        reachable = false;
    }
    return {};
}

auto BodyElaborator::transfer_statement(const ASTControlTransfer& source) noexcept
    -> AnalysisResult<void> {
    switch (source.kind) {
        case ASTControlTransferKind::Return: {
            if (!value_boundary_loop_depths.empty()) {
                static_cast<void>(fail(
                    source.keyword_span,
                    DiagnosticCode::FlowTransferValueBranch,
                    "return cannot cross a value-control branch"
                ));
                reachable = false;
                return {};
            }
            auto value = std::optional<SemanticExpression>();
            if (source.value.has_value()) {
                auto built = expression(*source.value, result_type);
                if (!built.has_value()) {
                    return std::unexpected(built.error());
                }
                if (result_type.has_value()
                    && is_void_type(draft(), *result_type)
                    && !is_void_type(draft(), built->type())
                    && !does_not_complete(*built)) {
                    return std::unexpected(fail(
                        source.span,
                        DiagnosticCode::TypeReturnValue,
                        "void callable requires a void return operand"
                    ));
                }
                if (!result_type.has_value()) {
                    auto inferred = infer_value_type(*built, ast.expression(*source.value).span);
                    if (!inferred.has_value()) {
                        return std::unexpected(inferred.error());
                    }
                    result_type = *inferred;
                } else {
                    auto coerced =
                        coerce_to(*built, *result_type, ast.expression(*source.value).span);
                    if (!coerced.has_value()) {
                        return std::unexpected(coerced.error());
                    }
                }
                if (is_void_type(draft(), built->type())) {
                    auto consumed = consume_pending(*built, ast.expression(*source.value).span);
                    if (!consumed.has_value()) {
                        return std::unexpected(consumed.error());
                    }
                    value = take_built(*built, ast.expression(*source.value).span);
                } else {
                    auto operand =
                        consume_value(*built, ast.expression(*source.value).span, AccessMode::Read);
                    if (!operand.has_value()) {
                        return std::unexpected(operand.error());
                    }
                    value = std::move(*operand);
                }
            } else {
                if (!result_type.has_value()) {
                    result_type = draft().intern_builtin_type(BuiltinType::Void);
                } else if (!is_void_type(draft(), *result_type)) {
                    return std::unexpected(fail(
                        source.span,
                        DiagnosticCode::TypeMissingReturnValue,
                        "value-returning callable must return a value"
                    ));
                }
            }
            append_statement(
                SemReturn {value.has_value() ? std::optional(std::move(*value)) : std::nullopt},
                source.span
            );
            reachable = false;
            return {};
        }
        case ASTControlTransferKind::Break: {
            if (loops.empty()) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowBreakOutsideLoop,
                    "break is only valid inside a loop"
                ));
            }
            if (!value_boundary_loop_depths.empty()
                && loops.size() <= value_boundary_loop_depths.back()) {
                static_cast<void>(fail(
                    source.keyword_span,
                    DiagnosticCode::FlowTransferValueBranch,
                    "break cannot cross a value-control branch"
                ));
                reachable = false;
                return {};
            }
            if (source.value.has_value()) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowTransferValueBranch,
                    "break cannot carry a value"
                ));
            }
            loops.back().has_break |= reachable && reference_path_reachable;
            append_statement(SemBreak {}, source.span);
            reachable = false;
            return {};
        }
        case ASTControlTransferKind::Continue: {
            if (loops.empty()) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowContinueOutsideLoop,
                    "continue is only valid inside a loop"
                ));
            }
            if (!value_boundary_loop_depths.empty()
                && loops.size() <= value_boundary_loop_depths.back()) {
                static_cast<void>(fail(
                    source.keyword_span,
                    DiagnosticCode::FlowTransferValueBranch,
                    "continue cannot cross a value-control branch"
                ));
                reachable = false;
                return {};
            }
            if (source.value.has_value()) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::FlowTransferValueBranch,
                    "continue cannot carry a value"
                ));
            }
            loops.back().has_continue |= reachable && reference_path_reachable;
            append_statement(SemContinue {}, source.span);
            reachable = false;
            return {};
        }
        case ASTControlTransferKind::Throw: {
            if (!source.value.has_value()) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectThrowType,
                    "throw requires a payload value"
                ));
            }
            auto payload = expression(*source.value);
            if (!payload.has_value()) {
                return std::unexpected(payload.error());
            }
            auto value =
                consume_value(*payload, ast.expression(*source.value).span, AccessMode::Read);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            const auto* failure_type = std::get_if<TypeID>(&value->type.construction());
            if (failure_type == nullptr || !is_failure_payload_type(draft(), *failure_type)) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectThrowType,
                    "throw payload must have a concrete nominal value type"
                ));
            }
            const auto& target = failure_context_for_current_path();
            draft().add_failure_member(target.term, *failure_type);
            append_statement(SemThrow {std::move(*value), *failure_type}, source.span);
            reachable = false;
            return {};
        }
        case ASTControlTransferKind::Rethrow: {
            if (catches.empty()) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectRethrowContext,
                    "rethrow is only valid inside a catch arm"
                ));
            }
            if (source.value.has_value()) {
                return std::unexpected(fail(
                    source.span,
                    DiagnosticCode::EffectRethrowContext,
                    "rethrow cannot carry a replacement payload"
                ));
            }
            const auto& caught = catches.back();
            const auto& target = failure_context_for_current_path();
            draft().add_failure_contribution(target.term, caught.failures);
            append_statement(SemRethrow {}, source.span);
            reachable = false;
            return {};
        }
    }
    std::unreachable();
}
