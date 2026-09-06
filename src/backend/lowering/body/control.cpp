module carven:backend.lowering.body.control.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace body_lowering {
auto BodyLowerer::structured_expression(
    const SemanticExpression& source,
    ResultDestination result,
    StatementSequence& destination
) noexcept -> void {
    std::visit(
        Overloaded {
            [&](const SemIf& value) noexcept { lower_if(value, result, destination); },
            [&](const SemMatch& value) noexcept { lower_match(value, result, destination); },
            [&](const SemTry& value) noexcept { lower_try(value, result, destination); },
            [](const auto&) static noexcept {
                invariant_violation("expected a structured semantic expression");
            },
        },
        source.value
    );
}

auto BodyLowerer::guarded_region(
    const SemanticRegion& source,
    const std::optional<SemanticExpression>& guard,
    const ResultDestination& result,
    RegionExit& done
) noexcept -> StatementSequence {
    auto guarded = StatementSequence();
    auto test = std::optional<TargetExpr>();
    auto known = std::optional<bool>();
    if (guard.has_value()) {
        test = condition(*guard, guarded);
        if (!test) {
            return guarded;
        }
        known = known_boolean(*guard);
        if (known == false) {
            guarded.emit(generated_statement(TargetDiscardStmt {.expression = std::move(*test)}));
            return guarded;
        }
    }
    if (!guarded.continues()) {
        return guarded;
    }
    auto selected = region(source, result);
    if (result.use != ResultUse::Return && selected.continues()) {
        done.used = true;
        selected.terminate(generated_statement(
            TargetGotoStmt {.label = done.label, .role = TargetJumpRole::RegionExit}
        ));
    }
    if (test.has_value() && !known.has_value()) {
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = std::move(*test), .body = std::move(selected).finish()});
        guarded.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    } else {
        if (test.has_value()) {
            guarded.emit(generated_statement(TargetDiscardStmt {.expression = std::move(*test)}));
        }
        guarded.append(std::move(selected));
    }
    return guarded;
}

auto BodyLowerer::lower_if(
    const SemIf& value,
    ResultDestination result,
    StatementSequence& destination
) noexcept -> void {
    const auto lower_branch = [&](this const auto& self,
                                  std::size_t index) noexcept -> StatementSequence {
        if (index == value.branches.size()) {
            return value.otherwise.has_value() ? region(**value.otherwise, result)
                                               : StatementSequence();
        }
        const auto& branch = value.branches[index];
        auto statements = StatementSequence();
        auto test = condition(branch.condition, statements);
        if (!statements.continues()) {
            return statements;
        }
        if (const auto known = known_boolean(branch.condition)) {
            statements.emit(
                generated_statement(TargetDiscardStmt {.expression = std::move(*test)})
            );
            statements.append(*known ? region(branch.body, result) : self(index + 1uz));
            return statements;
        }
        auto branches = std::vector<TargetIfBranch>();
        auto selected = region(branch.body, result);
        auto alternative = self(index + 1uz);
        const auto continues = selected.continues() || alternative.continues();
        branches.push_back({.condition = std::move(*test), .body = std::move(selected).finish()});
        statements.emit(
            generated_statement(
                TargetIfStmt {
                    .branches = std::move(branches),
                    .else_body = alternative.empty()
                        ? std::nullopt
                        : std::optional {std::move(alternative).finish()}
                }
            ),
            continues
        );
        return statements;
    };
    destination.append(lower_branch(0uz));
}

auto BodyLowerer::lower_arm(
    std::vector<PatternSelection> selections,
    std::span<const LocalBindingID> bindings,
    const SemanticRegion& source,
    const std::optional<SemanticExpression>& guard,
    const ResultDestination& result,
    RegionExit& done
) noexcept -> StatementSequence {
    auto statements = StatementSequence();
    auto projections = std::vector<PatternProjection>();
    cache_pattern_projections(selections, projections, statements);
    if (selections.size() == 1uz) {
        const auto& selection = selections.front();
        auto chosen = StatementSequence();
        for (const auto binding : bindings) {
            chosen.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::ConstValue,
                    .maybe_unused = true,
                    .name = binding_names.at(binding),
                    .type = context.lower_type(body.binding(binding).type),
                    .initializer = pattern_binding_expression(selection, binding)
                }
            ));
        }
        chosen.append(guarded_region(source, guard, result, done));
        auto condition = pattern_condition(selection);
        if (condition.has_value()) {
            auto branches = std::vector<TargetIfBranch>();
            branches.push_back(
                {.condition = std::move(*condition), .body = std::move(chosen).finish()}
            );
            statements.emit(generated_statement(
                TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
            ));
        } else {
            statements.append(std::move(chosen));
        }
        return statements;
    }
    const auto selected = names.fresh(TargetTemporaryNameKind::Logic);
    statements.emit(generated_statement(
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
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = true,
                .name = binding_names.at(binding),
                .type = context.optional_type(context.lower_type(body.binding(binding).type)),
                .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
            }
        ));
    }
    auto alternatives = std::vector<TargetIfBranch>();
    auto fallback = std::optional<std::vector<TargetStmt>>();
    for (const auto& selection : selections) {
        auto chosen = StatementSequence();
        for (const auto binding : bindings) {
            chosen.emit(statement_expression(call_member(
                name_expression(binding_names.at(binding)),
                "emplace",
                target_expressions(pattern_binding_expression(selection, binding))
            )));
        }
        chosen.emit(generated_statement(
            TargetAssignmentStmt {
                .target = name_expression(selected),
                .op = TargetAssignmentOperator::Assign,
                .value = bool_expression(true)
            }
        ));
        auto condition = pattern_condition(selection);
        if (condition.has_value()) {
            alternatives.push_back(
                {.condition = std::move(*condition), .body = std::move(chosen).finish()}
            );
        } else {
            fallback = std::move(chosen).finish();
            break;
        }
    }
    if (alternatives.empty()) {
        if (fallback.has_value()) {
            for (auto& statement : *fallback) {
                statements.emit(std::move(statement));
            }
        }
    } else {
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(alternatives), .else_body = std::move(fallback)}
        ));
    }
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back(
        {.condition = name_expression(selected),
         .body = guarded_region(source, guard, result, done).finish()}
    );
    statements.emit(generated_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
    for (const auto binding : bindings) {
        delayed_bindings.erase(binding);
    }
    return statements;
}

auto BodyLowerer::lower_match(
    const SemMatch& value,
    const ResultDestination& result,
    StatementSequence& destination
) noexcept -> void {
    auto done = RegionExit {.label = names.fresh(TargetTemporaryNameKind::MatchDone)};
    auto scope = StatementSequence();
    const auto subject = names.fresh(TargetTemporaryNameKind::Owner);
    auto subject_value = expression(*value.subject, scope);
    if (!scope.continues()) {
        destination.append(std::move(scope));
        return;
    }
    scope.emit(generated_statement(
        TargetVariableStmt {
            .binding = value.subject_is_place ? TargetVariableBinding::RvalueReference
                                              : TargetVariableBinding::ConstValue,
            .maybe_unused = true,
            .name = subject,
            .type = context.intrinsic_type(TargetSymbol::Auto),
            .initializer = std::move(*subject_value)
        }
    ));
    for (const auto& arm : value.arms) {
        if (!scope.continues()) {
            break;
        }
        if (!arm.reachable) {
            continue;
        }
        auto statements = StatementSequence();
        auto selections = lower_pattern(
            arm.pattern,
            {.root = subject, .dereference_root = false, .payload_path = {}}
        );
        statements =
            lower_arm(std::move(selections), arm.bindings, arm.body, arm.guard, result, done);
        scope.block(std::move(statements));
    }
    if (scope.continues()) {
        scope.terminate(generated_statement(
            TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
        ));
    }
    destination.block(std::move(scope));
    if (done.used) {
        destination.resume(done.label, TargetJumpRole::RegionExit);
    }
}

auto BodyLowerer::lower_try(
    const SemTry& value,
    const ResultDestination& result,
    StatementSequence& destination
) noexcept -> void {
    if (context.plan().failure_abi().members(value.protected_failures.resolved()).empty()) {
        destination.append(region(*value.body, result));
        return;
    }
    const auto storage = names.fresh(TargetTemporaryNameKind::Try);
    const auto handler = names.fresh(TargetTemporaryNameKind::Try);
    auto done = RegionExit {.label = names.fresh(TargetTemporaryNameKind::CatchDone)};
    const auto failures = context.plan().failure_abi().members(value.protected_failures.resolved());
    destination.emit(generated_statement(
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
    if (result.use != ResultUse::Return && protected_body.continues()) {
        done.used = true;
        protected_body.terminate(generated_statement(
            TargetGotoStmt {.label = done.label, .role = TargetJumpRole::RegionExit}
        ));
    }
    destination.block(std::move(protected_body));
    const auto handler_used = failure_destination->used;
    failure_destination = outer;
    if (!handler_used) {
        if (done.used) {
            destination.resume(done.label, TargetJumpRole::RegionExit);
        }
        return;
    }
    destination.resume(handler, TargetJumpRole::FailureTransfer);
    for (const auto& arm : value.arms) {
        if (!destination.continues()) {
            break;
        }
        if (context.plan().failure_abi().members(arm.accepted_failures.resolved()).empty()) {
            continue;
        }
        auto statements = StatementSequence();
        auto selections = std::vector<PatternSelection>();
        const auto add = [&](TypeID type, std::optional<PatternID> pattern) noexcept {
            const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
            statements.emit(generated_statement(
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
            if (const auto* pattern = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                add(pattern->type.resolved(), pattern->inner);
            } else {
                for (const auto type :
                     context.plan().failure_abi().members(arm.accepted_failures.resolved())) {
                    add(type, std::nullopt);
                }
            }
        }
        const auto previous_caught = caught_failure;
        caught_failure =
            CaughtFailure {.storage = storage, .failures = arm.accepted_failures.resolved()};
        statements.append(
            lower_arm(std::move(selections), arm.bindings, arm.body, arm.guard, result, done)
        );
        caught_failure = previous_caught;
        destination.block(std::move(statements));
    }
    transfer_failure(storage, value.residual_failures.resolved(), destination);
    if (done.used) {
        destination.resume(done.label, TargetJumpRole::RegionExit);
    }
}

} // namespace body_lowering
