module carven:semantic.analysis.elaboration.statements;

import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import std;

enum class MutationTargetStatus {
    Mutable,
    Immutable,
    NotAssignable,
    Error,
};

enum class ValueBranchKind {
    If,
    Match,
};

struct BuiltValueBranch final {
    HIRBlockID block;
    HIRExprID result;
    bool rejected_transfer;
};

struct BuiltBlock final {
    HIRBlockID block;
    bool rejected_transfer;
};

auto mutation_target_status(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept
    -> MutationTargetStatus;

auto diagnose_mutation_target(
    ModuleAnalysis& module_analysis,
    HIRExprID id,
    Span span,
    std::string_view immutable_message = "cannot modify an immutable value",
    DiagnosticCode immutable_code = DiagnosticCode::AccessImmutable
) noexcept -> void;

auto elaborate_expression_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTExprID value,
    Span statement_span,
    BodyControl control
) noexcept -> HIRStmtID;

auto elaborate_transfer(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTControlTransfer& transfer,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTControlTransfer& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTExprStatement& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTTestOperationStmt& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTVariableDecl& statement,
    Span span,
    BodyControl control
) noexcept -> std::optional<HIRStmtID>;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTAssignment& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTUpdate& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const CppRegion& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTWhileStmt& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTForStmt& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTIfForm& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTMatchForm& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTTryForm& statement,
    Span span,
    BodyControl control
) noexcept -> HIRStmtID;

auto elaborate_assignment(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTAssignment& assignment,
    BodyControl control
) noexcept -> HIRStmtID;

auto elaborate_update(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTUpdate& update,
    BodyControl control
) noexcept -> HIRStmtID;

auto elaborate_binding(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTVariableDecl& declaration,
    Span statement_span,
    BodyControl control
) noexcept -> std::optional<HIRStmtID>;

auto block_value(ModuleAnalysis& module_analysis, HIRBlockID id) noexcept -> HIRExprID;

auto infer_return_type(
    ModuleAnalysis& module_analysis,
    const ReturnTypeInference& inference,
    Span fallback_span
) noexcept -> HIRTypeID;

auto append_block(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    HIRBlock block
) noexcept -> HIRBlockID;

auto build_block_contents(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    Span span,
    std::span<const ASTStmtID> source_statements,
    std::optional<ASTExprID> source_result,
    BodyControl control,
    std::optional<ValueBranchKind> value_branch
) noexcept -> BuiltBlock;

auto build_block(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBlockID block_id,
    BodyControl control
) noexcept -> HIRBlockID;

auto while_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTWhileStmt& loop,
    Span statement_span,
    BodyControl control
) noexcept -> HIRStmtID;

auto for_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTForStmt& loop,
    Span statement_span,
    BodyControl control
) noexcept -> HIRStmtID;

auto build_branch(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBranchBlockID block_id,
    BodyControl control
) noexcept -> HIRBlockID;

auto build_value_branch(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBranchBlockID block_id,
    BodyControl control,
    ValueBranchKind kind
) noexcept -> BuiltValueBranch;
