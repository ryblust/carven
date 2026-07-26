module carven:semantic.analysis.elaboration.statements.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.patterns;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.statements;
import :semantic.analysis.elaboration.types;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.visit;
import std;

namespace {

constexpr auto lower_binding_kind(ASTBindingKind kind) noexcept -> HIRBindingKind {
    switch (kind) {
        case ASTBindingKind::Let:   return HIRBindingKind::Let;
        case ASTBindingKind::Var:   return HIRBindingKind::Var;
        case ASTBindingKind::Const: break;
    }
    std::unreachable();
}

} // namespace

auto elaborate_expression_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTExprID value,
    Span statement_span,
    BodyControl control
) noexcept -> HIRStmtID {
    auto& builder = module_analysis.builder();
    const auto built = build_expression(module_analysis, scopes, control, value);
    return builder.append_statement({
        .origin = module_analysis.origin(statement_span),
        .value = HIRExprStmt {.expression = built},
    });
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTTestOperationStmt& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto name = module_analysis.spelling(statement.keyword_span);
    const auto minimum = statement.kind == ASTTestOperationKind::Fail ? 0uz : 1uz;
    const auto maximum = statement.kind == ASTTestOperationKind::Fail ? 1uz : 2uz;
    if (statement.arguments.size() < minimum || statement.arguments.size() > maximum) {
        module_analysis.emit(
            span,
            std::format(
                "'{}' expects {}",
                name,
                statement.kind == ASTTestOperationKind::Fail ? "zero or one argument"
                                                             : "one or two arguments"
            ),
            DiagnosticCode::TestArgumentCount
        );
    }

    auto arguments = std::vector<HIRExprID>();
    arguments.reserve(statement.arguments.size());
    for (const auto argument : statement.arguments) {
        arguments.push_back(build_expression(module_analysis, scopes, control, argument));
    }
    const auto exact_builtin = [&](HIRExprID expression_id, HIRBuiltinType expected) noexcept {
        const auto& type_value =
            builder.type(expression_type(module_analysis, expression_id)).value;
        if (std::holds_alternative<HIRErrorTypeValue>(type_value)) {
            return true;
        }
        const auto* builtin_type = std::get_if<HIRBuiltinTypeValue>(&type_value);
        return builtin_type != nullptr && builtin_type->kind == expected;
    };

    auto condition = std::optional<HIRExprID>();
    auto message = std::optional<HIRExprID>();
    if (statement.kind == ASTTestOperationKind::Fail) {
        if (!arguments.empty()) {
            message = arguments.front();
        }
    } else {
        if (!arguments.empty()) {
            condition = arguments.front();
        }
        if (arguments.size() >= 2) {
            message = arguments[1];
        }
    }
    if (condition.has_value() && !exact_builtin(*condition, HIRBuiltinType::Bool)) {
        module_analysis.emit(
            ast.expression(statement.arguments.front()).span,
            "inline-test condition must have type bool",
            DiagnosticCode::TestConditionType
        );
    }
    if (message.has_value() && !exact_builtin(*message, HIRBuiltinType::Str)) {
        const auto source_index = statement.kind == ASTTestOperationKind::Fail ? 0uz : 1uz;
        module_analysis.emit(
            ast.expression(statement.arguments[source_index]).span,
            "inline-test message must have type str",
            DiagnosticCode::TestMessageType
        );
    }

    const auto origin = module_analysis.origin(statement.keyword_span);
    if (statement.kind == ASTTestOperationKind::Fail) {
        return builder.append_statement({
            .origin = origin,
            .value = HIRTestFailStmt {.message = message},
        });
    }
    const auto resolved_condition =
        condition.has_value() ? *condition : module_analysis.recover_expression(span);
    const auto condition_source = condition.has_value()
        ? module_analysis.spelling(ast.expression(statement.arguments.front()).span)
        : std::string_view();
    if (statement.kind == ASTTestOperationKind::Check) {
        return builder.append_statement({
            .origin = origin,
            .value = HIRTestCheckStmt {
                .condition = resolved_condition,
                .message = message,
                .condition_source = builder.intern_string(condition_source),
            },
        });
    }
    return builder.append_statement({
        .origin = origin,
        .value = HIRTestRequireStmt {
            .condition = resolved_condition,
            .message = message,
            .condition_source = builder.intern_string(condition_source),
        },
    });
}

auto elaborate_binding(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTVariableDecl& declaration,
    Span statement_span,
    BodyControl control
) noexcept -> std::optional<HIRStmtID> {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    auto target = HIRBindingTarget {HIRDiscardBindingTarget {}};
    auto symbol = std::optional<SymbolID>();
    auto binding_name = std::optional<std::string_view>();
    auto binding_name_span = std::optional<Span>();
    if (const auto* named = std::get_if<ASTNamedBindingTarget>(&declaration.target)) {
        const auto name = module_analysis.spelling(named->name_span);
        binding_name = name;
        binding_name_span = named->name_span;
        symbol = module_analysis.append_symbol({
            .name = builder.intern_string(name),
            .module_id = std::nullopt,
            .role = SemanticSymbolRole::Local,
            .parent = std::nullopt,
        });
        builder.define_symbol_binding(
            *symbol,
            declaration.kind == ASTBindingKind::Const ? SemanticBindingRole::CompileTime
                                                      : SemanticBindingRole::Owner,
            declaration.kind == ASTBindingKind::Var
        );
        target = HIRNamedBindingTarget {
            .symbol = *symbol,
        };
    }
    const auto proof = declaration.kind == ASTBindingKind::Const
        ? std::optional(builder.begin_expression_proof())
        : std::nullopt;
    const auto declared_type = declaration.type.has_value()
        ? std::optional<HIRTypeID> {build_type(module_analysis, scopes, control, *declaration.type)}
        : std::nullopt;
    const auto failure_checkpoint = declaration.kind == ASTBindingKind::Const
        ? std::optional(module_analysis.error_checkpoint())
        : std::nullopt;
    const auto invalid_constant_initializer = declaration.kind == ASTBindingKind::Const
        && declaration.initializer.has_value()
        && constant_initializer_requires_body_scope(ast, *declaration.initializer);
    if (invalid_constant_initializer) {
        module_analysis.emit(
            ast.expression(*declaration.initializer).span,
            "const initializer is not a supported compile-time constant expression",
            DiagnosticCode::ConstInitializer
        );
    }
    const auto value = invalid_constant_initializer
        ? module_analysis.recover_expression(ast.expression(*declaration.initializer).span)
        : (declaration.initializer.has_value()
               ? (declared_type.has_value() ? build_expected_expression(
                                                  module_analysis,
                                                  scopes,
                                                  control,
                                                  *declaration.initializer,
                                                  *declared_type,
                                                  ExpectedExpressionUsage::Binding
                                              )
                                            : build_expression(
                                                  module_analysis,
                                                  scopes,
                                                  control,
                                                  *declaration.initializer
                                              ))
               : module_analysis.recover_expression(declaration.span));
    const auto binding_type = require_value_type(
        module_analysis,
        declared_type.value_or(expression_type(module_analysis, value)),
        declaration.type.has_value()
            ? ast.type(*declaration.type).span
            : (declaration.initializer.has_value() ? ast.expression(*declaration.initializer).span
                                                   : declaration.span),
        ValueTypeRole::Binding
    );
    if (symbol.has_value()) {
        set_symbol_type(module_analysis, *symbol, binding_type);
    }
    if (declaration.kind == ASTBindingKind::Const) {
        const auto constant = builder.expression(value).constant;
        if (!constant.has_value() && !module_analysis.error_observed_since(*failure_checkpoint)) {
            module_analysis.emit(
                declaration.initializer.has_value() ? ast.expression(*declaration.initializer).span
                                                    : declaration.span,
                "const initializer is not a supported compile-time constant expression",
                DiagnosticCode::ConstInitializer
            );
        }
        builder.finish_expression_proof(*proof);
        if (symbol.has_value() && constant.has_value()) {
            builder.define_symbol_constant(*symbol, *constant);
        }
    }
    if (symbol.has_value()) {
        if (!scopes.bind(*binding_name, *symbol)) {
            module_analysis.emit(
                *binding_name_span,
                "duplicate local name",
                DiagnosticCode::NameDuplicateLocal
            );
        } else {
            builder.record_symbol_lint_candidate(
                *symbol,
                module_analysis.origin(*binding_name_span)
            );
        }
    }
    if (declaration.kind == ASTBindingKind::Const) {
        return std::nullopt;
    }
    return builder.append_statement({
        .origin = module_analysis.origin(statement_span),
        .value = HIRBindingStmt {
            .kind = lower_binding_kind(declaration.kind),
            .target = std::move(target),
            .type = binding_type,
            .initializer = value,
        },
    });
}

auto elaborate_transfer(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTControlTransfer& transfer,
    BodyControl control
) noexcept -> HIRStmtID {
    auto& builder = module_analysis.builder();
    if (transfer.kind == ASTControlTransferKind::Break) {
        const auto statement = builder.append_statement({
            .origin = module_analysis.origin(transfer.span),
            .value = HIRBreakStmt {},
        });
        if (control.loop_depth == 0) {
            module_analysis.emit(
                transfer.keyword_span,
                "'break' is only allowed inside a loop",
                DiagnosticCode::FlowBreakOutsideLoop
            );
            if (control.value_branch != nullptr) {
                control.value_branch->reject_transfer();
            }
        } else if (control.value_branch != nullptr
                   && !control.value_branch->contains_loop(control.loop_depth)) {
            module_analysis.emit(
                transfer.keyword_span,
                "'break' cannot leave a value-producing branch",
                DiagnosticCode::FlowTransferValueBranch
            );
            control.value_branch->reject_transfer();
        }
        return statement;
    }
    if (transfer.kind == ASTControlTransferKind::Continue) {
        const auto statement = builder.append_statement({
            .origin = module_analysis.origin(transfer.span),
            .value = HIRContinueStmt {},
        });
        if (control.loop_depth == 0) {
            module_analysis.emit(
                transfer.keyword_span,
                "'continue' is only allowed inside a loop",
                DiagnosticCode::FlowContinueOutsideLoop
            );
            if (control.value_branch != nullptr) {
                control.value_branch->reject_transfer();
            }
        } else if (control.value_branch != nullptr
                   && !control.value_branch->contains_loop(control.loop_depth)) {
            module_analysis.emit(
                transfer.keyword_span,
                "'continue' cannot leave a value-producing branch",
                DiagnosticCode::FlowTransferValueBranch
            );
            control.value_branch->reject_transfer();
        }
        return statement;
    }
    if (transfer.kind == ASTControlTransferKind::Throw) {
        const auto value = transfer.value.has_value()
            ? build_expression(module_analysis, scopes, control, *transfer.value)
            : module_analysis.recover_expression(transfer.span);
        const auto failure_type = expression_type(module_analysis, value);
        const auto& failure_value = builder.type(failure_type).value;
        if (!std::holds_alternative<HIRStructTypeValue>(failure_value)
            && !std::holds_alternative<HIREnumTypeValue>(failure_value)) {
            module_analysis.emit(
                transfer.span,
                "'throw' requires a copyable nominal struct or enum value",
                DiagnosticCode::EffectThrowType
            );
        }
        return builder.append_statement({
            .origin = module_analysis.origin(transfer.span),
            .value = HIRThrowStmt {
                .value = value,
                .failure_type = failure_type,
            },
        });
    }
    if (transfer.kind == ASTControlTransferKind::Rethrow) {
        const auto statement = builder.append_statement({
            .origin = module_analysis.origin(transfer.span),
            .value = HIRRethrowStmt {},
        });
        if (!control.permits_rethrow) {
            module_analysis.emit(
                transfer.keyword_span,
                "'rethrow' is only valid inside a catch handler",
                DiagnosticCode::EffectRethrowContext
            );
        }
        return statement;
    }
    if (control.value_branch != nullptr) {
        module_analysis.emit(
            transfer.keyword_span,
            "'return' cannot leave a value-producing branch",
            DiagnosticCode::FlowTransferValueBranch
        );
        control.value_branch->reject_transfer();
    }
    auto value = std::optional<HIRExprID>();
    if (transfer.value.has_value()) {
        if (!control.expected_return.has_value()
            || is_void(module_analysis, *control.expected_return)) {
            value = build_expression(module_analysis, scopes, control, *transfer.value);
        } else {
            value = expression_expected_diagnosing(
                module_analysis,
                scopes,
                control,
                *transfer.value,
                *control.expected_return,
                DiagnosticCode::TypeReturnMismatch,
                "returned value has an incompatible type",
                ExpectedExpressionUsage::Return
            );
        }
    }
    if (control.expected_return.has_value()) {
        const auto result = *control.expected_return;
        const auto foreign_result =
            std::holds_alternative<HIRForeignTypeValue>(builder.type(result).value);
        if (is_void(module_analysis, result) && value.has_value()) {
            module_analysis.emit(
                transfer.span,
                "a void function cannot return a value",
                DiagnosticCode::TypeReturnValue
            );
        } else if (!is_void(module_analysis, result) && !foreign_result && !value.has_value()) {
            module_analysis.emit(
                transfer.span,
                "a value-returning function must return a value",
                DiagnosticCode::TypeMissingReturnValue
            );
        }
    }
    const auto origin = module_analysis.origin(transfer.span);
    if (control.return_inference != nullptr) {
        control.return_inference->record({
            .origin = origin,
            .type = value.has_value()
                ? std::optional<HIRTypeID> {expression_type(module_analysis, *value)}
                : std::nullopt,
        });
    }
    return builder.append_statement({
        .origin = origin,
        .value = HIRReturnStmt {.value = value},
    });
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTControlTransfer& statement,
    Span,
    BodyControl control
) noexcept -> HIRStmtID {
    return elaborate_transfer(module_analysis, scopes, statement, control);
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTExprStatement& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID {
    return elaborate_expression_statement(
        module_analysis,
        scopes,
        statement.expression,
        span,
        control
    );
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTVariableDecl& statement,
    Span span,
    BodyControl control
) noexcept -> std::optional<HIRStmtID> {
    return elaborate_binding(module_analysis, scopes, statement, span, control);
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTAssignment& statement,
    Span,
    BodyControl control
) noexcept -> HIRStmtID {
    return elaborate_assignment(module_analysis, scopes, statement, control);
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTUpdate& statement,
    Span,
    BodyControl control
) noexcept -> HIRStmtID {
    return elaborate_update(module_analysis, scopes, statement, control);
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack&,
    const CppRegion& region,
    Span span,
    BodyControl
) noexcept -> HIRStmtID {
    auto& builder = module_analysis.builder();
    return builder.append_statement({
        .origin = module_analysis.origin(span),
        .value = HIRCppStmt {
            .bytes = builder.intern_string(module_analysis.spelling(region.body_span)),
        },
    });
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTWhileStmt& loop,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID {
    return while_statement(module_analysis, scopes, loop, span, control);
}

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTForStmt& loop,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID {
    return for_statement(module_analysis, scopes, loop, span, control);
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
    const auto exhaustive = analyze_match(module_analysis, subject_type, arms, match.span);
    const auto id = builder.append_statement({
        .origin = module_analysis.origin(span),
        .value = HIRMatchStmt {
            .subject = subject,
            .arms = std::move(arms),
            .exhaustive = exhaustive,
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

auto build_block(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBlockID block_id,
    BodyControl control
) noexcept -> HIRBlockID {
    const auto ast = module_analysis.syntax();
    const auto& block = ast.block(block_id);
    return build_block_contents(
               module_analysis,
               scopes,
               block.span,
               block.statements,
               std::nullopt,
               control,
               std::nullopt
    )
        .block;
}

auto append_block(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    HIRBlock block
) noexcept -> HIRBlockID {
    auto& builder = module_analysis.builder();
    block.scope = scopes.current_scope();
    return builder.append_block(std::move(block));
}

auto build_block_contents(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    Span span,
    std::span<const ASTStmtID> source_statements,
    std::optional<ASTExprID> source_result,
    BodyControl control,
    std::optional<ValueBranchKind> value_branch
) noexcept -> BuiltBlock {
    const auto ast = module_analysis.syntax();
    auto branch_state = std::optional<ValueBranchState>();
    if (value_branch.has_value()) {
        branch_state.emplace();
        control = inside_value_branch(control, *branch_state);
    }
    const auto block_scope = scopes.enter_scope();
    auto statements = std::vector<HIRStmtID>();
    for (const auto statement_id : source_statements) {
        const auto& statement = ast.statement(statement_id);
        const auto id = std::visit(
            [&](const auto& value) noexcept -> std::optional<HIRStmtID> {
                return build_statement(module_analysis, scopes, value, statement.span, control);
            },
            statement.value
        );
        if (id.has_value()) {
            statements.push_back(*id);
        }
    }
    auto result = source_result.has_value() ? std::optional<HIRExprID> {build_expression(
                                                  module_analysis,
                                                  scopes,
                                                  control,
                                                  *source_result
                                              )}
                                            : std::nullopt;
    if (value_branch.has_value() && !result.has_value()) {
        if (!branch_state->rejected_transfer()) {
            module_analysis.emit(
                span,
                *value_branch == ValueBranchKind::If
                    ? "value if branch must end with a result expression"
                    : "value match arm must end with a result expression",
                DiagnosticCode::FlowValueBranchResult
            );
        }
        result = module_analysis.recover_expression(span);
    }
    const auto block = append_block(
        module_analysis,
        scopes,
        {
            .origin = module_analysis.origin(span),
            .scope = scopes.current_scope(),
            .statements = std::move(statements),
            .result = result,
        }
    );
    return {
        .block = block,
        .rejected_transfer = branch_state.has_value() && branch_state->rejected_transfer(),
    };
}
