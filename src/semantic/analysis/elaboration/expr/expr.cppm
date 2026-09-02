module carven:semantic.analysis.elaboration.expr;

import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import std;

struct BuiltExpression final {
    ProgramOriginID origin;
    HIRTypeID type;
    std::optional<HIRConstant> constant;
    HIRExprValue value;
};

enum class ExpectedExpressionUsage {
    Binding,
    Return,
    CallArgument,
    Construction,
    General,
};

auto constant_initializer_requires_body_scope(const ASTView& ast, ASTExprID id) noexcept -> bool;

auto build_construction_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstructionExpr& construction,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID id,
    std::optional<HIRTypeID> expected = std::nullopt
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTLiteral& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    BodyControl control,
    const ASTNameExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTContextualCaseExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTGroupExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTArrayExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstructionExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTPrefixExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin,
    std::optional<HIRTypeID> expected = std::nullopt
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTAccessExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTBinaryExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTCastExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTCallExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTLambdaExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin,
    std::optional<HIRTypeID> expected
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTPropagationExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTIndexExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTMemberExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto member_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTMemberExpr& value,
    ASTExprID id,
    ProgramOriginID expression_origin,
    HIRExprID operand
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTIfForm& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTMatchForm& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTTryForm& value,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID;

auto append_expression(ModuleAnalysis& module_analysis, BuiltExpression expression) noexcept
    -> HIRExprID;

auto expression_constant(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept
    -> std::optional<HIRConstant>;

auto expression_type(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept -> HIRTypeID;

auto constant_integer(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept
    -> std::optional<HIRIntegerConstant>;

auto build_expected_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID id,
    HIRTypeID expected,
    ExpectedExpressionUsage usage = ExpectedExpressionUsage::General
) noexcept -> HIRExprID;

auto expression_expected_diagnosing(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID id,
    HIRTypeID expected,
    DiagnosticCode mismatch_code,
    std::string_view mismatch_message,
    ExpectedExpressionUsage usage = ExpectedExpressionUsage::General
) noexcept -> HIRExprID;
