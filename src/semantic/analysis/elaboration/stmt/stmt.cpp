module carven:semantic.analysis.elaboration.stmt.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.region;
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
import :support.visit;
import std;

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
