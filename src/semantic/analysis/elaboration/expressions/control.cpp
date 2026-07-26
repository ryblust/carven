module carven:semantic.analysis.elaboration.expressions.control.impl;

import :frontend.ast.control;
import :frontend.ast.expr;
import :frontend.ast.region;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.patterns;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.statements;
import :semantic.analysis.elaboration.types;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.type;
import :support.visit;
import std;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTIfForm& conditional,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    auto branches = std::vector<HIRConditionalBranch>();
    auto result = std::optional<HIRTypeID>();
    auto first_branch_result = std::optional<HIRExprID>();
    for (const auto& source_branch : conditional.branches) {
        const auto condition =
            build_expression(module_analysis, scopes, control, source_branch.condition);
        if (!is_bool(module_analysis, expression_type(module_analysis, condition))
            && !is_opaque_or_error(module_analysis, expression_type(module_analysis, condition))) {
            module_analysis.emit(
                ast.expression(source_branch.condition).span,
                "if condition must have type bool",
                DiagnosticCode::TypeConditionBool
            );
        }
        const auto body = build_value_branch(
            module_analysis,
            scopes,
            source_branch.body,
            control,
            ValueBranchKind::If
        );
        const auto branch_type = expression_type(module_analysis, body.result);
        if (!result.has_value()) {
            result = branch_type;
        } else if (!compatible(module_analysis, *result, branch_type)) {
            module_analysis.emit(
                conditional.span,
                "if branches have incompatible types",
                DiagnosticCode::TypeIfBranch
            );
            result = error_type(module_analysis, conditional.span);
        } else {
            defer_callable_compatibility(
                module_analysis,
                body.result,
                *first_branch_result,
                builder.block(body.block).origin,
                DiagnosticCode::TypeIfBranch,
                "if branches have incompatible types"
            );
        }
        if (!first_branch_result.has_value()) {
            first_branch_result = body.result;
        }
        branches.push_back({.condition = condition, .body = body.block});
    }
    auto else_branch = std::optional<HIRBlockID>();
    if (conditional.else_branch.has_value()) {
        const auto body = build_value_branch(
            module_analysis,
            scopes,
            *conditional.else_branch,
            control,
            ValueBranchKind::If
        );
        else_branch = body.block;
        const auto else_type = expression_type(module_analysis, body.result);
        if (result.has_value() && !compatible(module_analysis, *result, else_type)) {
            module_analysis.emit(
                conditional.span,
                "if branches have incompatible types",
                DiagnosticCode::TypeIfBranch
            );
            result = error_type(module_analysis, conditional.span);
        } else if (result.has_value() && !branches.empty()) {
            defer_callable_compatibility(
                module_analysis,
                body.result,
                *first_branch_result,
                builder.block(body.block).origin,
                DiagnosticCode::TypeIfBranch,
                "if branches have incompatible types"
            );
        }
    } else {
        module_analysis.emit(
            conditional.span,
            "value if requires a final else branch",
            DiagnosticCode::TypeIfMissingElse
        );
        result = error_type(module_analysis, conditional.span);
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result.has_value() ? *result : error_type(module_analysis, conditional.span),
            .constant = std::nullopt,
            .value = HIRIfExpr {
                .branches = std::move(branches),
                .else_branch = else_branch,
            },
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTMatchForm& match,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto subject = build_expression(module_analysis, scopes, control, match.subject);
    const auto subject_type = require_value_type(
        module_analysis,
        expression_type(module_analysis, subject),
        ast.expression(match.subject).span,
        ValueTypeRole::MatchSubject
    );
    auto arms = std::vector<HIRMatchArm>();
    auto result = std::optional<HIRTypeID>();
    auto first_arm_result = std::optional<HIRExprID>();
    for (const auto& arm : match.arms) {
        const auto arm_scope = scopes.enter_scope();
        const auto arm_scope_id = scopes.current_scope();
        const auto pattern_id =
            build_pattern(module_analysis, scopes, control, arm.pattern, subject_type);
        auto guard = std::optional<HIRExprID>();
        if (arm.guard.has_value()) {
            guard = expression_expected_diagnosing(
                module_analysis,
                scopes,
                control,
                arm.guard->expression,
                builtin(module_analysis, arm.guard->keyword_span, HIRBuiltinType::Bool),
                DiagnosticCode::TypeConditionBool,
                "match guard must have type bool"
            );
        }
        const auto arm_body = std::visit(
            Overloaded {
                [&](ASTExprID expression_value) noexcept -> BuiltValueBranch {
                    const auto value =
                        build_expression(module_analysis, scopes, control, expression_value);
                    const auto body = append_block(
                        module_analysis,
                        scopes,
                        {
                            .origin = module_analysis.origin(ast.expression(expression_value).span),
                            .scope = scopes.current_scope(),
                            .statements = {},
                            .result = value,
                        }
                    );
                    return {
                        .block = body,
                        .result = value,
                        .rejected_transfer = false,
                    };
                },
                [&](ASTBranchBlockID branch) noexcept -> BuiltValueBranch {
                    return build_value_branch(
                        module_analysis,
                        scopes,
                        branch,
                        control,
                        ValueBranchKind::Match
                    );
                },
                [&](const ASTControlTransfer& transfer) noexcept -> BuiltValueBranch {
                    auto branch_state = ValueBranchState();
                    const auto branch_control = inside_value_branch(control, branch_state);
                    const auto statement =
                        elaborate_transfer(module_analysis, scopes, transfer, branch_control);
                    if (!branch_state.rejected_transfer()) {
                        module_analysis.emit(
                            transfer.span,
                            "value match arm must end with a result expression",
                            DiagnosticCode::FlowValueBranchResult
                        );
                    }
                    const auto result = module_analysis.recover_expression(transfer.span);
                    const auto body = append_block(
                        module_analysis,
                        scopes,
                        {
                            .origin = module_analysis.origin(transfer.span),
                            .scope = scopes.current_scope(),
                            .statements = {statement},
                            .result = result,
                        }
                    );
                    return {
                        .block = body,
                        .result = result,
                        .rejected_transfer = branch_state.rejected_transfer(),
                    };
                },
            },
            arm.body.value
        );
        const auto arm_type = expression_type(module_analysis, arm_body.result);
        if (!result.has_value()) {
            result = arm_type;
        } else if (!compatible(module_analysis, *result, arm_type)) {
            module_analysis.emit(
                arm.span,
                "match arms have incompatible result types",
                DiagnosticCode::TypeMatchArm
            );
            result = error_type(module_analysis, arm.span);
        } else {
            defer_callable_compatibility(
                module_analysis,
                arm_body.result,
                *first_arm_result,
                builder.block(arm_body.block).origin,
                DiagnosticCode::TypeMatchArm,
                "match arms have incompatible result types"
            );
        }
        if (!first_arm_result.has_value()) {
            first_arm_result = arm_body.result;
        }
        arms.push_back({
            .scope = arm_scope_id,
            .pattern = pattern_id,
            .guard = guard,
            .body = arm_body.block,
        });
    }
    const auto exhaustive = analyze_match(module_analysis, subject_type, arms, match.span);
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result.has_value() ? *result : error_type(module_analysis, match.span),
            .constant = std::nullopt,
            .value = HIRMatchExpr {
                .subject = subject,
                .arms = std::move(arms),
                .exhaustive = exhaustive,
            },
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTTryForm& attempt,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    auto body_state = ValueBranchState();
    const auto body_control = inside_value_branch(control, body_state);
    const auto body = build_branch(module_analysis, scopes, attempt.body, body_control);
    if (!builder.block(body).result.has_value() && !body_state.rejected_transfer()) {
        module_analysis.emit(
            ast.branch_block(attempt.body).span,
            "value try body must end with a result expression",
            DiagnosticCode::FlowValueBranchResult
        );
    }
    auto result_type = expression_type(module_analysis, block_value(module_analysis, body));
    auto arms = std::vector<HIRCatchArm>();
    for (const auto& source_arm : attempt.arms) {
        const auto arm_scope = scopes.enter_scope();
        const auto arm_scope_id = scopes.current_scope();
        auto arm_state = ValueBranchState();
        const auto catch_control = inside_catch(inside_value_branch(control, arm_state));
        auto subjects = std::vector<PatternAlternativeSubject>();
        auto alternative_types = std::vector<std::optional<HIRTypeID>>();
        for (const auto& atom : source_arm.pattern.alternatives) {
            if (const auto* wildcard = std::get_if<ASTCatchWildcardPattern>(&atom.value)) {
                subjects.push_back(WildcardPatternSubject {.span = wildcard->underscore_span});
                alternative_types.push_back(std::nullopt);
                continue;
            }
            const auto& typed = std::get<ASTCatchTypedPattern>(atom.value);
            const auto failure = build_type(module_analysis, scopes, catch_control, typed.type);
            subjects.push_back(
                PatternSubject {
                    .pattern = typed.inner,
                    .type = failure,
                }
            );
            alternative_types.push_back(failure);
        }
        const auto patterns =
            pattern_alternatives(module_analysis, scopes, catch_control, subjects);
        auto alternatives = std::vector<HIRCatchPatternAlternative>();
        for (auto index = 0uz; index < patterns.size(); ++index) {
            alternatives.push_back({
                .type = alternative_types[index],
                .inner = patterns[index],
            });
        }
        auto guard = std::optional<HIRExprID>();
        if (source_arm.guard.has_value()) {
            guard = expression_expected_diagnosing(
                module_analysis,
                scopes,
                catch_control,
                source_arm.guard->expression,
                builtin(module_analysis, source_arm.guard->keyword_span, HIRBuiltinType::Bool),
                DiagnosticCode::TypeConditionBool,
                "catch guard must have type bool"
            );
        }
        const auto arm_body = std::visit(
            Overloaded {
                [&](ASTExprID value) noexcept {
                    const auto expression_value =
                        build_expression(module_analysis, scopes, catch_control, value);
                    return append_block(
                        module_analysis,
                        scopes,
                        {
                            .origin = module_analysis.origin(ast.expression(value).span),
                            .scope = scopes.current_scope(),
                            .statements = {},
                            .result = expression_value,
                        }
                    );
                },
                [&](ASTBranchBlockID value) noexcept {
                    return build_branch(module_analysis, scopes, value, catch_control);
                },
                [&](const ASTControlTransfer& value) noexcept {
                    const auto transfer =
                        elaborate_transfer(module_analysis, scopes, value, catch_control);
                    return append_block(
                        module_analysis,
                        scopes,
                        {
                            .origin = module_analysis.origin(value.span),
                            .scope = scopes.current_scope(),
                            .statements = {transfer},
                            .result = std::nullopt,
                        }
                    );
                },
            },
            source_arm.body.value
        );
        if (builder.block(arm_body).result.has_value()) {
            const auto arm_type = expression_type(module_analysis, *builder.block(arm_body).result);
            if (!compatible(module_analysis, result_type, arm_type)) {
                module_analysis.emit(
                    source_arm.span,
                    "try and catch arms have incompatible result types",
                    DiagnosticCode::TypeMatchArm
                );
                result_type = error_type(module_analysis, source_arm.span);
            } else {
                defer_callable_compatibility(
                    module_analysis,
                    *builder.block(arm_body).result,
                    block_value(module_analysis, body),
                    builder.block(arm_body).origin,
                    DiagnosticCode::TypeMatchArm,
                    "try and catch arms have incompatible result types"
                );
            }
        }
        arms.push_back({
            .scope = arm_scope_id,
            .alternatives = std::move(alternatives),
            .guard = guard,
            .body = arm_body,
        });
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result_type,
            .constant = std::nullopt,
            .value = HIRTryExpr {
                .body = body,
                .arms = std::move(arms),
            },
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack&,
    BodyControl,
    const CppRegion& region,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = foreign_type(module_analysis, ast.expression(id).span),
            .constant = std::nullopt,
            .value = HIRCppExpr {
                .bytes = builder.intern_string(module_analysis.spelling(region.body_span)),
            },
        }
    );
}
