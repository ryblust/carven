module carven:semantic.analysis.elaboration.stmt.control.impl;

import :frontend.ast.control;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.stmt;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.patterns;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.stmt;
import :semantic.analysis.elaboration.types;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

auto build_branch(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBranchBlockID block_id,
    BodyControl control
) noexcept -> HIRBlockID {
    const auto ast = module_analysis.syntax();
    const auto& block = ast.branch_block(block_id);
    return build_block_contents(
               module_analysis,
               scopes,
               block.span,
               block.statements,
               block.result,
               control,
               std::nullopt
    )
        .block;
}

auto build_value_branch(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBranchBlockID block_id,
    BodyControl control,
    ValueBranchKind kind
) noexcept -> BuiltValueBranch {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto& source = ast.branch_block(block_id);
    const auto built = build_block_contents(
        module_analysis,
        scopes,
        source.span,
        source.statements,
        source.result,
        control,
        kind
    );
    const auto& block = builder.block(built.block);

    if (!block.result.has_value()) {
        invariant_violation("expression block was built without a result expression");
    }

    return {
        .block = built.block,
        .result = *block.result,
        .rejected_transfer = built.rejected_transfer,
    };
}

auto infer_return_type(
    ModuleAnalysis& module_analysis,
    const ReturnTypeInference& inference,
    Span fallback_span
) noexcept -> HIRTypeID {
    const auto& builder = module_analysis.builder();
    auto inferred = std::optional<HIRTypeID>();
    auto incompatible = false;
    for (const auto& observation : inference.observations()) {
        if (!observation.type.has_value()) {
            continue;
        }
        if (!inferred.has_value()) {
            inferred = observation.type;
        } else if (!compatible(module_analysis, *inferred, *observation.type)) {
            incompatible = true;
        }
    }
    auto result = inferred.has_value()
        ? *inferred
        : builtin(module_analysis, fallback_span, HIRBuiltinType::Void);
    if (incompatible) {
        module_analysis.emit(
            fallback_span,
            "lambda return values do not infer one compatible result type",
            DiagnosticCode::LambdaSignatureInference
        );
        result = error_type(module_analysis, fallback_span);
    }
    const auto foreign_result =
        std::holds_alternative<HIRForeignTypeValue>(builder.type(result).value);
    for (const auto& observation : inference.observations()) {
        const auto span = builder.provenance().origin(observation.origin).span;
        if (is_void(module_analysis, result) && observation.type.has_value()) {
            module_analysis.emit(
                span,
                "a void function cannot return a value",
                DiagnosticCode::TypeReturnValue
            );
        } else if (!is_void(module_analysis, result)
                   && !foreign_result
                   && !observation.type.has_value()) {
            module_analysis.emit(
                span,
                "a value-returning function must return a value",
                DiagnosticCode::TypeMissingReturnValue
            );
        } else if (observation.type.has_value()
                   && !compatible(module_analysis, result, *observation.type)) {
            module_analysis.emit(
                span,
                "returned value has an incompatible type",
                DiagnosticCode::TypeReturnMismatch
            );
        }
    }
    return result;
}

auto block_value(ModuleAnalysis& module_analysis, HIRBlockID id) noexcept -> HIRExprID {
    auto& builder = module_analysis.builder();
    if (const auto& block = builder.block(id); block.result) {
        return *block.result;
    } else {
        return module_analysis.recover_expression(builder.provenance().origin(block.origin).span);
    }
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTIfForm& conditional,
    Span,
    BodyControl control
) noexcept -> HIRStmtID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    auto branches = std::vector<HIRConditionalBranch>();
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
        branches.push_back({
            .condition = condition,
            .body = build_branch(module_analysis, scopes, source_branch.body, control),
        });
    }
    auto else_branch = std::optional<HIRBlockID>();
    if (conditional.else_branch.has_value()) {
        else_branch = build_branch(module_analysis, scopes, *conditional.else_branch, control);
    }
    return builder.append_statement({
        .origin = module_analysis.origin(conditional.span),
        .value = HIRIfStmt {
            .branches = std::move(branches),
            .else_branch = else_branch,
        },
    });
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTMatchForm& match,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID {
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
        const auto body = std::visit(
            Overloaded {
                [&](ASTExprID value) noexcept -> HIRBlockID {
                    return append_block(
                        module_analysis,
                        scopes,
                        {
                            .origin = module_analysis.origin(ast.expression(value).span),
                            .scope = scopes.current_scope(),
                            .statements = {},
                            .result = build_expression(module_analysis, scopes, control, value),
                        }
                    );
                },
                [&](ASTBranchBlockID value) noexcept -> HIRBlockID {
                    return build_branch(module_analysis, scopes, value, control);
                },
                [&](const ASTControlTransfer& value) noexcept -> HIRBlockID {
                    const auto transfer =
                        elaborate_transfer(module_analysis, scopes, value, control);
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
            arm.body.value
        );
        arms.push_back({
            .scope = arm_scope_id,
            .pattern = pattern_id,
            .guard = guard,
            .body = body,
        });
    }
    auto coverage = analyze_match(module_analysis, subject_type, arms, match.span);
    const auto id = builder.append_statement({
        .origin = module_analysis.origin(span),
        .value = HIRMatchStmt {
            .subject = subject,
            .arms = std::move(arms),
            .coverage = std::move(coverage),
        },
    });
    return id;
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTTryForm& attempt,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto body =
        builder.normalize_void_block(build_branch(module_analysis, scopes, attempt.body, control));
    auto arms = std::vector<HIRCatchArm>();
    for (const auto& source_arm : attempt.arms) {
        const auto catch_control = inside_catch(control);
        const auto arm_scope = scopes.enter_scope();
        const auto arm_scope_id = scopes.current_scope();
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
                .origin = module_analysis.origin(source_arm.pattern.alternatives[index].span),
                .type = alternative_types[index],
                .inner = patterns[index],
            });
        }
        analyze_catch_pattern(module_analysis, alternatives);
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
                [&](ASTExprID value) noexcept -> HIRBlockID {
                    const auto result =
                        build_expression(module_analysis, scopes, catch_control, value);
                    return append_block(
                        module_analysis,
                        scopes,
                        {
                            .origin = module_analysis.origin(ast.expression(value).span),
                            .scope = scopes.current_scope(),
                            .statements = {builder.append_statement({
                                .origin = builder.expression(result).origin,
                                .value = HIRExprStmt {.expression = result},
                            })},
                            .result = std::nullopt,
                        }
                    );
                },
                [&](ASTBranchBlockID value) noexcept -> HIRBlockID {
                    return builder.normalize_void_block(
                        build_branch(module_analysis, scopes, value, catch_control)
                    );
                },
                [&](const ASTControlTransfer& value) noexcept -> HIRBlockID {
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
        arms.push_back({
            .origin = module_analysis.origin(source_arm.span),
            .scope = arm_scope_id,
            .alternatives = std::move(alternatives),
            .guard = guard,
            .body = arm_body,
        });
    }
    const auto expression_id = append_expression(
        module_analysis,
        {
            .origin = module_analysis.origin(span),
            .type = builtin(module_analysis, span, HIRBuiltinType::Void),
            .constant = std::nullopt,
            .value = HIRTryExpr {
                .body = body,
                .arms = std::move(arms),
            },
        }
    );
    return builder.append_statement({
        .origin = module_analysis.origin(span),
        .value = HIRExprStmt {.expression = expression_id},
    });
}
