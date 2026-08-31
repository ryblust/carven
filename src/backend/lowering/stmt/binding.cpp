module carven:backend.lowering.stmt.binding.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import std;

namespace {

auto lower_variable_binding(
    const TargetCallableLowerer& context,
    HIRBindingKind kind,
    const HIRNamedBindingTarget* named_target
) noexcept -> TargetVariableBinding {
    if (kind == HIRBindingKind::Var) {
        return TargetVariableBinding::MutableValue;
    }
    if (named_target == nullptr) {
        return TargetVariableBinding::ConstValue;
    }
    if (context.requires_mutable_value_binding(named_target->symbol)) {
        return TargetVariableBinding::MutableValue;
    }
    return TargetVariableBinding::ConstValue;
}

} // namespace

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRExprStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto* attempt =
        std::get_if<HIRTryExpr>(&context.source().expression(statement.expression).value);
    if (attempt != nullptr
        && is_void_type(context, context.source().expression(statement.expression).type)) {
        return lower_void_try(context, statement.expression, *attempt, control);
    }
    auto expression = lower_expression(context, statement.expression, control);
    auto statement_value =
        is_void_type(context, context.source().expression(statement.expression).type)
        ? TargetStmtValue {TargetExprStmt {.expression = expression.expression}}
        : TargetStmtValue {TargetDiscardStmt {.expression = expression.expression}};
    return with_prelude(context, std::move(expression.prelude), std::move(statement_value));
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRBindingStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto* named_target = std::get_if<HIRNamedBindingTarget>(&statement.target);
    const auto target_type = is_foreign_type(context, statement.type)
        ? intrinsic_type(context, TargetSymbol::Auto)
        : lower_type(context, statement.type);
    auto initializer = lower_expression(context, statement.initializer, control);
    const auto maybe_unused =
        named_target == nullptr || !symbol_is_used(context, named_target->symbol);
    return with_prelude(
        context,
        std::move(initializer.prelude),
        TargetVariableStmt {
            .binding = lower_variable_binding(context, statement.kind, named_target),
            .name = named_target == nullptr
                ? context.fresh_name(TargetTemporaryNameKind::Discard)
                : binding_identifier(
                      context,
                      named_target->symbol,
                      context.source().evaluation_effect(statement.initializer)
                  ),
            .type = target_type,
            .initializer = initializer.expression,
            .maybe_unused = maybe_unused,
        }
    );
}
