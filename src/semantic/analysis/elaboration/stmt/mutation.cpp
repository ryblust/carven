module carven:semantic.analysis.elaboration.stmt.mutation.impl;

import :frontend.ast.stmt;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.stmt;
import :semantic.analysis.elaboration.types;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.visit;
import std;

namespace {

constexpr auto lower_assignment_operator(ASTAssignmentOperator op) noexcept
    -> HIRAssignmentOperator {
    switch (op) {
        case ASTAssignmentOperator::Assign:     return HIRAssignmentOperator::Assign;
        case ASTAssignmentOperator::Add:        return HIRAssignmentOperator::Add;
        case ASTAssignmentOperator::Subtract:   return HIRAssignmentOperator::Subtract;
        case ASTAssignmentOperator::Multiply:   return HIRAssignmentOperator::Multiply;
        case ASTAssignmentOperator::Divide:     return HIRAssignmentOperator::Divide;
        case ASTAssignmentOperator::Remainder:  return HIRAssignmentOperator::Remainder;
        case ASTAssignmentOperator::BitwiseAnd: return HIRAssignmentOperator::BitwiseAnd;
        case ASTAssignmentOperator::BitwiseOr:  return HIRAssignmentOperator::BitwiseOr;
        case ASTAssignmentOperator::BitwiseXor: return HIRAssignmentOperator::BitwiseXor;
        case ASTAssignmentOperator::LeftShift:  return HIRAssignmentOperator::LeftShift;
        case ASTAssignmentOperator::RightShift: return HIRAssignmentOperator::RightShift;
    }
    std::unreachable();
}

constexpr auto lower_update_operator(ASTUpdateOperator op) noexcept -> HIRUpdateOperator {
    switch (op) {
        case ASTUpdateOperator::Increment: return HIRUpdateOperator::Increment;
        case ASTUpdateOperator::Decrement: return HIRUpdateOperator::Decrement;
    }
    std::unreachable();
}

} // namespace

auto elaborate_assignment(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTAssignment& assignment,
    BodyControl control
) noexcept -> HIRStmtID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto target = build_expression(module_analysis, scopes, control, assignment.target);
    diagnose_mutation_target(module_analysis, target, ast.expression(assignment.target).span);
    const auto value = build_expected_expression(
        module_analysis,
        scopes,
        control,
        assignment.value,
        expression_type(module_analysis, target)
    );
    if (assignment.op != ASTAssignmentOperator::Assign
        && !is_opaque_or_error(module_analysis, expression_type(module_analysis, target))) {
        const auto integer_only = assignment.op == ASTAssignmentOperator::Remainder
            || assignment.op == ASTAssignmentOperator::BitwiseAnd
            || assignment.op == ASTAssignmentOperator::BitwiseOr
            || assignment.op == ASTAssignmentOperator::BitwiseXor
            || assignment.op == ASTAssignmentOperator::LeftShift
            || assignment.op == ASTAssignmentOperator::RightShift;
        if (integer_only ? !is_integer(module_analysis, expression_type(module_analysis, target))
                         : !is_numeric(module_analysis, expression_type(module_analysis, target))) {
            module_analysis.emit(
                assignment.operator_span,
                integer_only ? "compound assignment requires an integer target"
                             : "compound assignment requires a numeric target",
                integer_only ? DiagnosticCode::TypeAssignmentInteger
                             : DiagnosticCode::TypeAssignmentNumeric
            );
        }
    }
    return builder.append_statement({
        .origin = module_analysis.origin(assignment.span),
        .value = HIRAssignmentStmt {
            .target = target,
            .op = lower_assignment_operator(assignment.op),
            .value = value,
        },
    });
}

auto elaborate_update(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTUpdate& update,
    BodyControl control
) noexcept -> HIRStmtID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto target = build_expression(module_analysis, scopes, control, update.target);
    diagnose_mutation_target(module_analysis, target, ast.expression(update.target).span);
    if (!is_integer(module_analysis, expression_type(module_analysis, target))
        && !is_opaque_or_error(module_analysis, expression_type(module_analysis, target))) {
        module_analysis.emit(
            ast.expression(update.target).span,
            "update target must have an integer type",
            DiagnosticCode::TypeUpdateInteger
        );
    }
    return builder.append_statement({
        .origin = module_analysis.origin(update.span),
        .value = HIRUpdateStmt {
            .op = lower_update_operator(update.op),
            .target = target,
        },
    });
}

auto diagnose_mutation_target(
    ModuleAnalysis& module_analysis,
    HIRExprID id,
    Span span,
    std::string_view immutable_message,
    DiagnosticCode immutable_code
) noexcept -> void {
    switch (mutation_target_status(module_analysis, id)) {
        case MutationTargetStatus::Mutable:
        case MutationTargetStatus::Error:   return;
        case MutationTargetStatus::Immutable:
            module_analysis.emit(span, std::string(immutable_message), immutable_code);
            return;
        case MutationTargetStatus::NotAssignable:
            module_analysis.emit(
                span,
                "mutation target is not assignable",
                DiagnosticCode::AccessNotAssignable
            );
            return;
    }
}

auto mutation_target_status(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept
    -> MutationTargetStatus {
    const auto builder = module_analysis.builder();
    const auto& expression = builder.expression(id);
    if (std::holds_alternative<HIRErrorTypeValue>(builder.type(expression.type).value)) {
        return MutationTargetStatus::Error;
    }
    return std::visit(
        Overloaded {
            [&](const HIRNameExpr& name) noexcept {
                if (builder.symbol_write_eligible(name.symbol)) {
                    return MutationTargetStatus::Mutable;
                }
                const auto role = builder.symbol_role(name.symbol);
                if (role == SemanticSymbolRole::Local
                    || role == SemanticSymbolRole::Parameter
                    || role == SemanticSymbolRole::LoopBinding) {
                    return MutationTargetStatus::Immutable;
                }
                return MutationTargetStatus::NotAssignable;
            },
            [&](const HIRIndexExpr& index) noexcept {
                return mutation_target_status(module_analysis, index.operand_id);
            },
            [&](const HIRMemberExpr& member) noexcept {
                const auto uses_scope = std::visit(
                    Overloaded {
                        [](const HIRUnresolvedMemberTarget& target) static noexcept {
                            return target.scope;
                        },
                        [](const HIRStructFieldTarget&) static noexcept { return false; },
                        [](const HIREnumCaseTarget&) static noexcept { return true; },
                    },
                    member.target
                );
                return uses_scope ? MutationTargetStatus::NotAssignable
                                  : mutation_target_status(module_analysis, member.operand_id);
            },
            [](const auto&) static noexcept { return MutationTargetStatus::NotAssignable; },
        },
        expression.value
    );
}
