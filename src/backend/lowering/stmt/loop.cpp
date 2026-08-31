module carven:backend.lowering.stmt.loop.impl;

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
import :semantic.hir.access;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :support.visit;
import std;

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
    auto initializer = std::optional<PreparedTargetStatement>();
    if (statement.initializer.has_value()) {
        initializer.emplace(prepare_target_statement(context, *statement.initializer, control)
                                .classify_for_initializer());
    }
    auto condition = std::optional<LoweredExpression>();
    if (statement.condition.has_value()) {
        condition = lower_expression(context, *statement.condition, control);
    }
    auto steps = std::vector<PreparedTargetStatement>();
    steps.reserve(statement.steps.size());
    for (const auto step : statement.steps) {
        steps.push_back(prepare_target_statement(context, step, control).classify_for_step());
    }
    const auto has_fast_initializer = !initializer.has_value() || initializer->is_for_initializer();
    const auto has_fast_steps =
        std::ranges::all_of(steps, [](const PreparedTargetStatement& step) static noexcept {
            return step.is_for_step();
        });
    if (has_fast_initializer
        && has_fast_steps
        && (!condition.has_value() || condition->prelude.empty())) {
        auto for_initializer = std::optional<TargetForInitializer>();
        if (initializer.has_value()) {
            for_initializer = std::move(*initializer).take_for_initializer();
        }
        auto for_steps = std::vector<TargetForStep>();
        for_steps.reserve(steps.size());
        for (auto& step : steps) {
            for_steps.push_back(std::move(step).take_for_step());
        }
        const auto loop_control = control.with_continue_destination(std::nullopt);
        return TargetForStmt {
            .initializer = std::move(for_initializer),
            .condition =
                condition.has_value() ? std::optional {condition->expression} : std::nullopt,
            .steps = std::move(for_steps),
            .body = lower_block(context, statement.body, loop_control),
        };
    }

    auto statement_ids = std::vector<TargetStmtID>();
    if (initializer.has_value()) {
        statement_ids.push_back(std::move(*initializer).publish(context));
    }
    auto published_step_ids = std::vector<TargetStmtID>();
    published_step_ids.reserve(steps.size());
    for (auto& step : steps) {
        published_step_ids.push_back(std::move(step).publish(context));
    }
    const auto continue_label = !published_step_ids.empty()
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
    loop_body.insert(loop_body.end(), published_step_ids.begin(), published_step_ids.end());
    const auto true_value = context.target().append_expression({
        .value = TargetLiteralExpr {.value = true},
    });
    statement_ids.push_back(context.target().append_lowering_statement(
        TargetWhileStmt {
            .condition = true_value,
            .body = std::move(loop_body),
        }
    ));
    return TargetBlockStmt {.statements = std::move(statement_ids), .scoped = true};
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
