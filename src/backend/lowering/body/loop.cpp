module carven:backend.lowering.body.loop.impl;

import :backend.generation.names;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.visit;
import std;

namespace body_lowering {
namespace {
auto append(std::vector<TargetStmt>& destination, std::vector<TargetStmt> source) noexcept -> void {
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end())
    );
}
}

auto BodyLowerer::lower_loop(
    const SemLoop<TypeID, FailureSetID>& value,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    auto initializer =
        region(*value.initializer, {.use = ResultUse::Discard, .storage = std::nullopt});
    if (!falls_through(initializer)) {
        destination.push_back(generated_statement(
            TargetBlockStmt {.statements = std::move(initializer), .scoped = true}
        ));
        return;
    }
    auto condition_statements = std::vector<TargetStmt>();
    auto condition = value.condition.has_value()
        ? this->condition(*value.condition, condition_statements)
        : bool_expression(true);
    if (!falls_through(condition_statements)) {
        append(initializer, std::move(condition_statements));
        destination.push_back(generated_statement(
            TargetBlockStmt {.statements = std::move(initializer), .scoped = true}
        ));
        return;
    }
    if (value.condition.has_value() && known_boolean(*value.condition) == false) {
        append(initializer, std::move(condition_statements));
        initializer.push_back(
            generated_statement(TargetDiscardStmt {.expression = std::move(condition)})
        );
        destination.push_back(generated_statement(
            TargetBlockStmt {.statements = std::move(initializer), .scoped = true}
        ));
        return;
    }
    const auto previous_continue_used = std::exchange(continue_label_used, false);
    auto body_statements =
        region(*value.body, {.use = ResultUse::Discard, .storage = std::nullopt});
    const auto has_continue = std::exchange(continue_label_used, previous_continue_used);
    auto steps = falls_through(body_statements) || has_continue
        ? region(*value.steps, {.use = ResultUse::Discard, .storage = std::nullopt})
        : std::vector<TargetStmt>();
    auto simple_steps = std::vector<TargetForStep>();
    const auto collect_step = [&](this const auto& self, TargetStmt& statement) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](TargetBlockStmt& value) noexcept {
                    return std::ranges::all_of(value.statements, [&](TargetStmt& child) noexcept {
                        return self(child);
                    });
                },
                [&](TargetExprStmt& value) noexcept {
                    simple_steps.push_back({.value = std::move(value)});
                    return true;
                },
                [&](TargetDiscardStmt& value) noexcept {
                    simple_steps.push_back({.value = std::move(value)});
                    return true;
                },
                [&](TargetAssignmentStmt& value) noexcept {
                    simple_steps.push_back({.value = std::move(value)});
                    return true;
                },
                [&](TargetUpdateStmt& value) noexcept {
                    simple_steps.push_back({.value = std::move(value)});
                    return true;
                },
                [](auto&) static noexcept { return false; },
            },
            statement.value
        );
    };
    // Check before moving clauses so the complex path retains its whole body.
    const auto simple = [&](this const auto& self, const TargetStmt& statement) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetBlockStmt& value) noexcept {
                    return std::ranges::all_of(value.statements, [&](const auto& child) noexcept {
                        return self(child);
                    });
                },
                [](const TargetExprStmt&) static noexcept { return true; },
                [](const TargetDiscardStmt&) static noexcept { return true; },
                [](const TargetAssignmentStmt&) static noexcept { return true; },
                [](const TargetUpdateStmt&) static noexcept { return true; },
                [](const auto&) static noexcept { return false; },
            },
            statement.value
        );
    };
    const auto direct_steps = std::ranges::all_of(steps, simple);
    if (direct_steps && condition_statements.empty()) {
        for (auto& step : steps) {
            static_cast<void>(collect_step(step));
        }
        if (value.initializer->statements.empty() && value.steps->statements.empty()) {
            initializer.push_back(generated_statement(
                TargetWhileStmt {
                    .condition = std::move(condition),
                    .body = std::move(body_statements)
                }
            ));
        } else {
            auto initial_clause = std::optional<TargetForInitializer>();
            if (initializer.size() == 1uz) {
                std::visit(
                    Overloaded {
                        [&](TargetVariableStmt& value) noexcept {
                            initial_clause = TargetForInitializer {.value = std::move(value)};
                        },
                        [&](TargetAssignmentStmt& value) noexcept {
                            initial_clause = TargetForInitializer {.value = std::move(value)};
                        },
                        [&](TargetExprStmt& value) noexcept {
                            initial_clause = TargetForInitializer {.value = std::move(value)};
                        },
                        [&](TargetDiscardStmt& value) noexcept {
                            initial_clause = TargetForInitializer {.value = std::move(value)};
                        },
                        [](auto&) static noexcept {},
                    },
                    initializer.front().value
                );
                if (initial_clause.has_value()) {
                    initializer.clear();
                }
            }
            initializer.push_back(generated_statement(
                TargetForStmt {
                    .initializer = std::move(initial_clause),
                    .condition = std::move(condition),
                    .steps = std::move(simple_steps),
                    .body = std::move(body_statements)
                }
            ));
        }
    } else {
        const auto step_label = names.fresh(TargetTemporaryNameKind::Continue);
        if (has_continue && !steps.empty()) {
            const auto redirect = [&](this const auto& self,
                                      std::vector<TargetStmt>& statements) noexcept -> void {
                for (auto& statement : statements) {
                    if (std::holds_alternative<TargetContinueStmt>(statement.value)) {
                        statement.value = TargetGotoStmt {
                            .label = step_label,
                            .role = TargetJumpRole::ForLoopContinue
                        };
                    } else if (auto* block = std::get_if<TargetBlockStmt>(&statement.value)) {
                        self(block->statements);
                    } else if (auto* branch = std::get_if<TargetIfStmt>(&statement.value)) {
                        for (auto& arm : branch->branches) {
                            self(arm.body);
                        }
                        if (branch->else_body.has_value()) {
                            self(*branch->else_body);
                        }
                    }
                }
            };
            redirect(body_statements);
        }
        auto iteration = std::move(condition_statements);
        auto exit_body = std::vector<TargetStmt>();
        exit_body.push_back(generated_statement(TargetBreakStmt {}));
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(condition)),
             .body = std::move(exit_body)}
        );
        iteration.push_back(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
        if (!body_statements.empty()) {
            iteration.push_back(generated_statement(
                TargetBlockStmt {.statements = std::move(body_statements), .scoped = true}
            ));
        }
        if (!steps.empty() && (has_continue || falls_through(iteration))) {
            if (has_continue) {
                iteration.push_back(generated_statement(
                    TargetLabelStmt {.label = step_label, .role = TargetJumpRole::ForLoopContinue}
                ));
            }
            append(iteration, std::move(steps));
        }
        initializer.push_back(generated_statement(
            TargetWhileStmt {.condition = bool_expression(true), .body = std::move(iteration)}
        ));
    }
    const auto scoped = initializer.size() != 1uz
        || (!std::holds_alternative<TargetForStmt>(initializer.front().value)
            && !std::holds_alternative<TargetWhileStmt>(initializer.front().value));
    destination.push_back(generated_statement(
        TargetBlockStmt {.statements = std::move(initializer), .scoped = scoped}
    ));
}

auto BodyLowerer::lower_range(
    const SemRangeLoop<TypeID, FailureSetID>& value,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    auto scope = std::vector<TargetStmt>();
    const auto index = names.fresh(TargetTemporaryNameKind::Operand);
    const auto limit = names.fresh(TargetTemporaryNameKind::Operand);
    const auto owner = names.fresh(TargetTemporaryNameKind::Owner);
    auto begin = value.end.has_value() ? operand(value.begin, scope, OperandUse::Snapshot)
                                       : expression(value.begin, scope);
    if (!falls_through(scope)) {
        destination.push_back(
            generated_statement(TargetBlockStmt {.statements = std::move(scope), .scoped = true})
        );
        return;
    }
    if (!value.end.has_value()) {
        scope.push_back(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::RvalueReference,
                .maybe_unused = false,
                .name = owner,
                .type = context.intrinsic_type(TargetSymbol::Auto),
                .initializer = std::move(begin)
            }
        ));
        const auto previous_used = continue_label_used;
        continue_label_used = false;
        auto iteration = region(*value.body, {.use = ResultUse::Discard, .storage = std::nullopt});
        continue_label_used = previous_used;
        scope.push_back(generated_statement(
            TargetRangeForStmt {
                .binding = value.access == AccessMode::Write
                    ? TargetVariableBinding::MutableReference
                    : !value.binding.has_value() ? TargetVariableBinding::ConstReference
                                                 : TargetVariableBinding::MutableValue,
                .maybe_unused = !value.binding.has_value(),
                .name = value.binding.has_value() ? binding_names.at(*value.binding) : index,
                .type = value.binding.has_value()
                    ? (value.access == AccessMode::Write
                           ? context.lower_type(body.binding(*value.binding).type)
                           : context.lower_parameter(
                                 CallableParameter {
                                     .access = AccessMode::Read,
                                     .type = body.binding(*value.binding).type
                                 }
                             ))
                    : context.intrinsic_type(TargetSymbol::Auto),
                .range = name_expression(owner),
                .body = std::move(iteration)
            }
        ));
        destination.push_back(
            generated_statement(TargetBlockStmt {.statements = std::move(scope), .scoped = true})
        );
        return;
    }
    auto initial = std::move(begin);
    const auto index_type = context.lower_type(value.begin.type);
    auto upper = expression(*value.end, scope);
    if (!falls_through(scope)) {
        destination.push_back(
            generated_statement(TargetBlockStmt {.statements = std::move(scope), .scoped = true})
        );
        return;
    }
    auto element = name_expression(index);
    scope.push_back(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .maybe_unused = false,
            .name = limit,
            .type = index_type,
            .initializer = std::move(upper)
        }
    ));
    const auto previous_continue_used = continue_label_used;
    continue_label_used = false;
    auto iteration = std::vector<TargetStmt>();
    if (value.binding.has_value()) {
        const auto id = *value.binding;
        iteration.push_back(generated_statement(
            TargetVariableStmt {
                .binding = value.access == AccessMode::Write
                    ? TargetVariableBinding::MutableReference
                    : TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = binding_names.at(id),
                .type = context.lower_type(body.binding(id).type),
                .initializer = std::move(element)
            }
        ));
    }
    append(iteration, region(*value.body, {.use = ResultUse::Discard, .storage = std::nullopt}));
    continue_label_used = previous_continue_used;
    auto steps = std::vector<TargetForStep>();
    steps.push_back(
        {.value = TargetUpdateStmt {
             .op = TargetUpdateOperator::Increment,
             .target = name_expression(index)
         }}
    );
    scope.push_back(generated_statement(
        TargetForStmt {
            .initializer =
                TargetForInitializer {
                    .value =
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::MutableValue,
                            .maybe_unused = false,
                            .name = index,
                            .type = index_type,
                            .initializer = std::move(initial)
                        }
                },
            .condition = binary_expression(
                name_expression(index),
                TargetBinaryOperator::Less,
                name_expression(limit)
            ),
            .steps = std::move(steps),
            .body = std::move(iteration),
        }
    ));
    destination.push_back(
        generated_statement(TargetBlockStmt {.statements = std::move(scope), .scoped = true})
    );
}

} // namespace body_lowering
