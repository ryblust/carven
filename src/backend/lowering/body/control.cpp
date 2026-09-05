module carven:backend.lowering.body.control.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace body_lowering {
namespace {
auto has_jump_to(std::span<const TargetStmt> statements, const TargetIdentifier& label) noexcept
    -> bool {
    struct Query final {
        const TargetIdentifier& label;
        auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
            const auto* jump = std::get_if<TargetGotoStmt>(&statement.value);
            return jump == nullptr || jump->label.spelling() != label.spelling();
        }
    };
    auto query = Query {.label = label};
    return !traverse_target_statements(statements, query);
}

auto append(std::vector<TargetStmt>& destination, std::vector<TargetStmt> source) noexcept -> void {
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end())
    );
}
}

// Lowering stops appending operations at a lexical exit. The last statement
// therefore determines continuation; labels admit incoming synthetic jumps.
auto falls_through(std::span<const TargetStmt> statements) noexcept -> bool {
    if (statements.empty()) {
        return true;
    }
    const auto has_break = [&](this const auto& self,
                               std::span<const TargetStmt> body) noexcept -> bool {
        return std::ranges::any_of(body, [&](const TargetStmt& statement) noexcept {
            if (std::holds_alternative<TargetBreakStmt>(statement.value)) {
                return true;
            }
            if (const auto* block = std::get_if<TargetBlockStmt>(&statement.value)) {
                return self(block->statements);
            }
            if (const auto* branch = std::get_if<TargetIfStmt>(&statement.value)) {
                return (branch->else_body.has_value() && self(*branch->else_body))
                    || std::ranges::any_of(branch->branches, [&](const auto& arm) noexcept {
                           return self(arm.body);
                       });
            }
            return false;
        });
    };
    const auto always = [](const TargetExpr& expression) static noexcept {
        const auto* literal = std::get_if<TargetLiteralExpr>(&expression.value);
        return literal != nullptr
            && std::get_if<bool>(&literal->value) != nullptr
            && std::get<bool>(literal->value);
    };
    return std::visit(
        Overloaded {
            [](const TargetReturnStmt&) static noexcept { return false; },
            [](const TargetBreakStmt&) static noexcept { return false; },
            [](const TargetContinueStmt&) static noexcept { return false; },
            [](const TargetGotoStmt&) static noexcept { return false; },
            [](const TargetUnreachableStmt&) static noexcept { return false; },
            [](const TargetRuntimeTrapStmt&) static noexcept { return false; },
            [](const TargetBlockStmt& block) static noexcept {
                return falls_through(block.statements);
            },
            [](const TargetIfStmt& branch) static noexcept {
                return !branch.else_body.has_value()
                    || falls_through(*branch.else_body)
                    || std::ranges::any_of(branch.branches, [](const auto& arm) static noexcept {
                           return falls_through(arm.body);
                       });
            },
            [&](const TargetWhileStmt& loop) noexcept {
                return !always(loop.condition) || has_break(loop.body);
            },
            [&](const TargetForStmt& loop) noexcept {
                return (loop.condition.has_value() && !always(*loop.condition))
                    || has_break(loop.body);
            },
            [](const auto&) static noexcept { return true; },
        },
        statements.back().value
    );
}

auto BodyLowerer::structured_expression(
    const SemIRExpression& source,
    ResultDestination result,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    std::visit(
        Overloaded {
            [&](const SemIf<TypeID, FailureSetID>& value) noexcept {
                lower_if(value, result, destination);
            },
            [&](const SemMatch<TypeID, FailureSetID>& value) noexcept {
                lower_match(value, result, destination);
            },
            [&](const SemTry<TypeID, FailureSetID>& value) noexcept {
                lower_try(value, result, destination);
            },
            [](const auto&) static noexcept {
                invariant_violation("expected a structured semantic expression");
            },
        },
        source.value
    );
}

auto BodyLowerer::guarded_region(
    const SemIRRegion& source,
    const std::optional<SemIRExpression>& guard,
    const ResultDestination& result,
    const TargetIdentifier& done
) noexcept -> std::vector<TargetStmt> {
    auto guarded = std::vector<TargetStmt>();
    auto test = std::optional<TargetExpr>();
    auto known = std::optional<bool>();
    if (guard.has_value()) {
        test = condition(*guard, guarded);
        known = known_boolean(*guard);
        if (known == false) {
            guarded.push_back(
                generated_statement(TargetDiscardStmt {.expression = std::move(*test)})
            );
            return guarded;
        }
    }
    if (!falls_through(guarded)) {
        return guarded;
    }
    auto selected = region(source, result);
    if (result.use != ResultUse::Return && falls_through(selected)) {
        selected.push_back(
            generated_statement(TargetGotoStmt {.label = done, .role = TargetJumpRole::RegionExit})
        );
    }
    if (test.has_value() && !known.has_value()) {
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = std::move(*test), .body = std::move(selected)});
        guarded.push_back(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    } else {
        if (test.has_value()) {
            guarded.push_back(
                generated_statement(TargetDiscardStmt {.expression = std::move(*test)})
            );
        }
        append(guarded, std::move(selected));
    }
    return guarded;
}

auto BodyLowerer::lower_if(
    const SemIf<TypeID, FailureSetID>& value,
    ResultDestination result,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    const auto lower_branch = [&](this const auto& self,
                                  std::size_t index) noexcept -> std::vector<TargetStmt> {
        if (index == value.branches.size()) {
            return value.otherwise.has_value() ? region(**value.otherwise, result)
                                               : std::vector<TargetStmt>();
        }
        const auto& branch = value.branches[index];
        auto statements = std::vector<TargetStmt>();
        auto test = condition(branch.condition, statements);
        if (!falls_through(statements)) {
            return statements;
        }
        if (const auto known = known_boolean(branch.condition)) {
            if (!std::holds_alternative<TargetLiteralExpr>(test.value)) {
                statements.push_back(
                    generated_statement(TargetDiscardStmt {.expression = std::move(test)})
                );
            }
            append(statements, *known ? region(branch.body, result) : self(index + 1uz));
            return statements;
        }
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = std::move(test), .body = region(branch.body, result)});
        auto alternative = self(index + 1uz);
        statements.push_back(generated_statement(
            TargetIfStmt {
                .branches = std::move(branches),
                .else_body =
                    alternative.empty() ? std::nullopt : std::optional {std::move(alternative)}
            }
        ));
        return statements;
    };
    append(destination, lower_branch(0uz));
}

auto BodyLowerer::lower_arm(
    std::vector<PatternSelection> selections,
    std::span<const LocalBindingID> bindings,
    const SemIRRegion& source,
    const std::optional<SemIRExpression>& guard,
    const ResultDestination& result,
    const TargetIdentifier& done
) noexcept -> std::vector<TargetStmt> {
    auto statements = std::vector<TargetStmt>();
    auto projections = std::vector<PatternProjection>();
    cache_pattern_projections(selections, projections, statements);
    if (selections.size() == 1uz) {
        const auto& selection = selections.front();
        auto chosen = std::vector<TargetStmt>();
        for (const auto binding : bindings) {
            chosen.push_back(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::ConstValue,
                    .maybe_unused = false,
                    .name = binding_names.at(binding),
                    .type = context.lower_type(body.binding(binding).type),
                    .initializer = pattern_binding_expression(selection, binding)
                }
            ));
        }
        append(chosen, guarded_region(source, guard, result, done));
        auto condition = pattern_condition(selection);
        if (condition.has_value()) {
            auto branches = std::vector<TargetIfBranch>();
            branches.push_back({.condition = std::move(*condition), .body = std::move(chosen)});
            statements.push_back(generated_statement(
                TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
            ));
        } else {
            append(statements, std::move(chosen));
        }
        return statements;
    }
    const auto selected = names.fresh(TargetTemporaryNameKind::Logic);
    statements.push_back(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = selected,
            .type = context.intrinsic_type(TargetSymbol::Bool),
            .initializer = bool_expression(false)
        }
    ));
    for (const auto binding : bindings) {
        delayed_bindings.insert(binding);
        statements.push_back(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = binding_names.at(binding),
                .type = context.optional_type(context.lower_type(body.binding(binding).type)),
                .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
            }
        ));
    }
    auto alternatives = std::vector<TargetIfBranch>();
    auto fallback = std::optional<std::vector<TargetStmt>>();
    for (const auto& selection : selections) {
        auto chosen = std::vector<TargetStmt>();
        for (const auto binding : bindings) {
            chosen.push_back(statement_expression(call_member(
                name_expression(binding_names.at(binding)),
                "emplace",
                target_expressions(pattern_binding_expression(selection, binding))
            )));
        }
        chosen.push_back(generated_statement(
            TargetAssignmentStmt {
                .target = name_expression(selected),
                .op = TargetAssignmentOperator::Assign,
                .value = bool_expression(true)
            }
        ));
        auto condition = pattern_condition(selection);
        if (condition.has_value()) {
            alternatives.push_back({.condition = std::move(*condition), .body = std::move(chosen)});
        } else {
            fallback = std::move(chosen);
            break;
        }
    }
    if (alternatives.empty()) {
        if (fallback.has_value()) {
            append(statements, std::move(*fallback));
        }
    } else {
        statements.push_back(generated_statement(
            TargetIfStmt {.branches = std::move(alternatives), .else_body = std::move(fallback)}
        ));
    }
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back(
        {.condition = name_expression(selected),
         .body = guarded_region(source, guard, result, done)}
    );
    statements.push_back(generated_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
    for (const auto binding : bindings) {
        delayed_bindings.erase(binding);
    }
    return statements;
}

auto BodyLowerer::lower_match(
    const SemMatch<TypeID, FailureSetID>& value,
    const ResultDestination& result,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    const auto done = names.fresh(TargetTemporaryNameKind::MatchDone);
    auto scope = std::vector<TargetStmt>();
    const auto subject = names.fresh(TargetTemporaryNameKind::Owner);
    auto subject_value = expression(*value.subject, scope);
    if (!falls_through(scope)) {
        append(destination, std::move(scope));
        return;
    }
    scope.push_back(generated_statement(
        TargetVariableStmt {
            .binding = value.subject_is_place ? TargetVariableBinding::RvalueReference
                                              : TargetVariableBinding::ConstValue,
            .maybe_unused = false,
            .name = subject,
            .type = context.intrinsic_type(TargetSymbol::Auto),
            .initializer = std::move(subject_value)
        }
    ));
    const auto subject_statement = scope.size() - 1uz;
    auto subject_used = false;
    for (const auto& arm : value.arms) {
        if (!arm.reachable) {
            continue;
        }
        auto statements = std::vector<TargetStmt>();
        auto selections = lower_pattern(
            arm.pattern,
            {.root = subject, .dereference_root = false, .payload_path = {}}
        );
        subject_used = subject_used
            || std::ranges::any_of(
                           selections,
                           [](const PatternSelection& selection) static noexcept {
                               return !selection.constraints.empty() || !selection.bindings.empty();
                           }
            );
        statements =
            lower_arm(std::move(selections), arm.bindings, arm.body, arm.guard, result, done);
        scope.push_back(generated_statement(
            TargetBlockStmt {.statements = std::move(statements), .scoped = true}
        ));
    }
    std::get<TargetVariableStmt>(scope[subject_statement].value).maybe_unused = !subject_used;
    if (falls_through(scope)) {
        scope.push_back(generated_statement(
            TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
        ));
    }
    destination.push_back(
        generated_statement(TargetBlockStmt {.statements = std::move(scope), .scoped = true})
    );
    if (result.use != ResultUse::Return && has_jump_to(destination, done)) {
        destination.push_back(
            generated_statement(TargetLabelStmt {.label = done, .role = TargetJumpRole::RegionExit})
        );
    }
}

auto BodyLowerer::lower_try(
    const SemTry<TypeID, FailureSetID>& value,
    const ResultDestination& result,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    if (context.plan().failure_abi().members(value.protected_failures).empty()) {
        append(destination, region(*value.body, result));
        return;
    }
    const auto storage = names.fresh(TargetTemporaryNameKind::Try);
    const auto handler = names.fresh(TargetTemporaryNameKind::Try);
    const auto done = names.fresh(TargetTemporaryNameKind::CatchDone);
    const auto failures = context.plan().failure_abi().members(value.protected_failures);
    destination.push_back(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = storage,
            .type = context.optional_type(context.variant_type(failures)),
            .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
        }
    ));
    const auto outer = failure_destination;
    failure_destination = FailureDestination {.storage = storage, .label = handler};
    auto protected_body = region(*value.body, result);
    if (result.use != ResultUse::Return && falls_through(protected_body)) {
        protected_body.push_back(
            generated_statement(TargetGotoStmt {.label = done, .role = TargetJumpRole::RegionExit})
        );
    }
    destination.push_back(generated_statement(
        TargetBlockStmt {.statements = std::move(protected_body), .scoped = true}
    ));
    failure_destination = outer;
    destination.push_back(generated_statement(
        TargetLabelStmt {.label = handler, .role = TargetJumpRole::FailureTransfer}
    ));
    for (const auto& arm : value.arms) {
        if (context.plan().failure_abi().members(arm.accepted_failures).empty()) {
            continue;
        }
        auto statements = std::vector<TargetStmt>();
        auto selections = std::vector<PatternSelection>();
        const auto add = [&](TypeID type, std::optional<PatternID> pattern) noexcept {
            const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
            statements.push_back(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .name = projection,
                    .type = context.pointer_type(context.intrinsic_type(TargetSymbol::Auto, true)),
                    .initializer = template_call_expression(
                        intrinsic_expression(TargetSymbol::StdGetIf),
                        {context.lower_type(type)},
                        target_expressions(
                            address_expression(dereference_expression(name_expression(storage)))
                        )
                    )
                }
            ));
            auto candidates = std::vector<PatternSelection>();
            if (pattern.has_value()) {
                candidates = lower_pattern(
                    *pattern,
                    {.root = projection, .dereference_root = true, .payload_path = {}}
                );
            } else {
                candidates.push_back(PatternSelection {});
            }
            for (auto& candidate : candidates) {
                candidate.failure_projection = projection;
                selections.push_back(std::move(candidate));
            }
        };
        for (const auto& alternative : arm.alternatives) {
            if (!alternative.reachable) {
                continue;
            }
            if (const auto* pattern =
                    std::get_if<SemTypedCatchPattern<TypeID>>(&alternative.pattern)) {
                add(pattern->type, pattern->inner);
            } else {
                for (const auto type :
                     context.plan().failure_abi().members(arm.accepted_failures)) {
                    add(type, std::nullopt);
                }
            }
        }
        const auto previous_caught = caught_failure;
        caught_failure = CaughtFailure {.storage = storage, .failures = arm.accepted_failures};
        append(
            statements,
            lower_arm(std::move(selections), arm.bindings, arm.body, arm.guard, result, done)
        );
        caught_failure = previous_caught;
        destination.push_back(generated_statement(
            TargetBlockStmt {.statements = std::move(statements), .scoped = true}
        ));
    }
    transfer_failure(storage, value.residual_failures, destination);
    if (result.use != ResultUse::Return && has_jump_to(destination, done)) {
        destination.push_back(
            generated_statement(TargetLabelStmt {.label = done, .role = TargetJumpRole::RegionExit})
        );
    }
}

} // namespace body_lowering
