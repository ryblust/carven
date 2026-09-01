module carven:semantic.analysis.elaboration.stmt.block.impl;

import :frontend.ast.stmt;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.stmt;
import :semantic.hir.stmt;
import std;

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
