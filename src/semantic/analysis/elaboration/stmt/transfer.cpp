module carven:semantic.analysis.elaboration.stmt.transfer.impl;

import :frontend.ast.control;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.stmt;
import :semantic.analysis.elaboration.types;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :semantic.hir.type;
import std;

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
        if (is_void(module_analysis, result) && value.has_value()) {
            module_analysis.emit(
                transfer.span,
                "a void function cannot return a value",
                DiagnosticCode::TypeReturnValue
            );
        } else if (!is_void(module_analysis, result) && !value.has_value()) {
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
