module carven:backend.lowering.statements.loop.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.statements;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.access;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :support.visit;
import std;

namespace {

auto simple_for_clause(TargetCallableLowerer& context, TargetStmtID statement) noexcept
    -> std::optional<TargetStmtID> {
    const auto& value = context.target().statement(statement).value;
    if (const auto* sequence = std::get_if<TargetBlockStmt>(&value)) {
        if (!sequence->scoped && sequence->statements.size() == 1) {
            return simple_for_clause(context, sequence->statements.front());
        }
        return std::nullopt;
    }
    return std::visit(
        [&](const auto& clause) noexcept -> std::optional<TargetStmtID> {
            using Value = std::remove_cvref_t<decltype(clause)>;
            if constexpr (std::same_as<Value, TargetExprStmt>
                          || std::same_as<Value, TargetDiscardStmt>
                          || std::same_as<Value, TargetVariableStmt>
                          || std::same_as<Value, TargetAssignmentStmt>
                          || std::same_as<Value, TargetUpdateStmt>) {
                return statement;
            }
            return std::nullopt;
        },
        value
    );
}

} // namespace

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRWhileStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto condition = lower_expression(context, statement.condition, control);
    const auto loop_control = control.with_continue_destination(std::nullopt);
    auto source_body = lower_block(context, statement.body, loop_control);
    if (condition.prelude.empty()) {
        return TargetWhileStmt {
            .condition = condition.expression,
            .body = std::move(source_body),
        };
    }
    const auto true_value = context.target().append_expression({
        .value = TargetLiteralExpr {.value = true},
    });
    const auto stop_condition = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id = condition.expression,
        },
    });
    const auto stop_statement = context.target().append_lowering_statement(TargetBreakStmt {});
    const auto stop_guard = context.target().append_lowering_statement(
        TargetIfStmt {
            .branches = {TargetIfBranch {.condition = stop_condition, .body = {stop_statement}}},
            .else_body = std::nullopt,
        }
    );
    condition.prelude.push_back(stop_guard);
    condition.prelude.insert(condition.prelude.end(), source_body.begin(), source_body.end());
    return TargetWhileStmt {
        .condition = true_value,
        .body = std::move(condition.prelude),
    };
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRCStyleForStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto lexical_scope = context.enter_scope(statement.scope);
    auto initializer = std::optional<TargetStmtID>();
    if (statement.initializer.has_value()) {
        initializer = lower_statement(context, *statement.initializer, control);
    }
    auto condition = std::optional<LoweredExpression>();
    if (statement.condition.has_value()) {
        condition = lower_expression(context, *statement.condition, control);
    }
    auto steps = std::vector<TargetStmtID>();
    for (const auto step : statement.steps) {
        steps.push_back(lower_statement(context, step, control));
    }
    const auto simple_initializer = initializer.has_value()
        ? simple_for_clause(context, *initializer)
        : std::optional<TargetStmtID> {};
    auto simple_steps = std::vector<TargetStmtID> {};
    simple_steps.reserve(steps.size());
    for (const auto step : steps) {
        const auto clause = simple_for_clause(context, step);
        if (!clause.has_value()) {
            simple_steps.clear();
            break;
        }
        simple_steps.push_back(*clause);
    }
    if ((!initializer.has_value() || simple_initializer.has_value())
        && simple_steps.size() == steps.size()
        && (!condition.has_value() || condition->prelude.empty())) {
        const auto loop_control = control.with_continue_destination(std::nullopt);
        return TargetForStmt {
            .initializer = simple_initializer,
            .condition =
                condition.has_value() ? std::optional {condition->expression} : std::nullopt,
            .steps = std::move(simple_steps),
            .body = lower_block(context, statement.body, loop_control),
        };
    }

    auto statements = std::vector<TargetStmtID>();
    if (initializer.has_value()) {
        statements.push_back(*initializer);
    }
    const auto continue_label = !steps.empty()
        ? std::optional<TargetIdentifier> {context.fresh_name(TargetTemporaryNameKind::Continue)}
        : std::optional<TargetIdentifier> {};
    auto continue_used = false;
    const auto continue_destination = continue_label.has_value()
        ? std::optional<ContinueDestination> {ContinueDestination {
              .label = *continue_label,
              .used = std::ref(continue_used)
          }}
        : std::nullopt;
    const auto loop_control = control.with_continue_destination(continue_destination);
    auto source_body = lower_block(context, statement.body, loop_control);
    auto loop_body = std::vector<TargetStmtID>();
    if (condition.has_value()) {
        loop_body.insert(loop_body.end(), condition->prelude.begin(), condition->prelude.end());
        const auto stop_condition = context.target().append_expression({
            .value = TargetPrefixExpr {
                .op = TargetPrefixOperator::LogicalNot,
                .operand_id = condition->expression,
            },
        });
        const auto stop_statement = context.target().append_lowering_statement(TargetBreakStmt {});
        loop_body.push_back(context.target().append_lowering_statement(
            TargetIfStmt {
                .branches =
                    {TargetIfBranch {.condition = stop_condition, .body = {stop_statement}}},
                .else_body = std::nullopt,
            }
        ));
    }
    loop_body.push_back(context.target().append_lowering_statement(
        TargetBlockStmt {.statements = std::move(source_body), .scoped = true}
    ));
    if (continue_used) {
        loop_body.push_back(context.target().append_lowering_statement(
            TargetLabelStmt {
                .label = *continue_label,
                .kind = TargetSyntheticControlKind::NormalizedForContinue,
            }
        ));
    }
    loop_body.insert(loop_body.end(), steps.begin(), steps.end());
    const auto true_value = context.target().append_expression({
        .value = TargetLiteralExpr {.value = true},
    });
    statements.push_back(context.target().append_lowering_statement(
        TargetWhileStmt {
            .condition = true_value,
            .body = std::move(loop_body),
        }
    ));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = true};
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRRangeForStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto* named = std::get_if<HIRNamedBindingTarget>(&statement.target);
    auto iterable = std::visit(
        Overloaded {
            [&](HIRExprID value) noexcept { return lower_expression(context, value, control); },
            [&](const HIRHalfOpenRange& range) noexcept {
                const auto arguments = std::array {
                    HIRCallArgument {.access = HIRAccessMode::Read, .expression = range.begin},
                    HIRCallArgument {.access = HIRAccessMode::Read, .expression = range.end},
                };
                return ordered_call(
                    context,
                    {.prelude = {},
                     .expression = name_expression(context, TargetSymbol::RuntimeIntegerRange)},
                    arguments,
                    false,
                    control
                );
            },
        },
        statement.iterable
    );
    const auto lexical_scope = context.enter_scope(statement.scope);
    const auto loop_control = control.with_continue_destination(std::nullopt);
    auto body = lower_block(context, statement.body, loop_control);
    const auto binding_mode = std::holds_alternative<HIRHalfOpenRange>(statement.iterable)
        ? TargetRangeBindingMode::ReadValue
        : statement.access == HIRAccessMode::Read ? TargetRangeBindingMode::ReadReference
                                                  : TargetRangeBindingMode::MutableReference;
    const auto maybe_unused = named == nullptr || !symbol_is_used(context, named->symbol);
    return with_prelude(
        context,
        std::move(iterable.prelude),
        TargetRangeForStmt {
            .binding_mode = binding_mode,
            .name = named == nullptr ? context.fresh_name(TargetTemporaryNameKind::Discard)
                                     : symbol_identifier(context, named->symbol),
            .type = lower_type(context, statement.type),
            .iterable = iterable.expression,
            .body = std::move(body),
            .maybe_unused = maybe_unused
        }
    );
}
