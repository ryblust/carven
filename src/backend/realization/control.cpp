module carven:backend.realization.control.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyRealizer::structured_expression(
    ConstructionExpressionID source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    std::visit(
        Overloaded {
            [&](const ConstructionConditional& value) noexcept {
                lower_if(value, result, destination);
            },
            [&](const ConstructionMatch& value) noexcept {
                lower_match(value, result, destination);
            },
            [&](const ConstructionTry& value) noexcept {
                lower_try(source, value, result, destination);
            },
            [](const auto&) static noexcept {
                invariant_violation("expected a structured semantic expression");
            },
        },
        construction.expression(source).value
    );
}

auto BodyRealizer::guarded_region(
    ConstructionRegionID source,
    const std::optional<ConstructionExpressionID>& guard,
    const LoweringResultDestination& result,
    RegionExit& done
) noexcept -> LoweringStmtBuilder {
    auto guarded = LoweringStmtBuilder();
    auto test = std::optional<LoweringPredicate>(LoweringKnownBool {true});
    if (guard.has_value()) {
        test = guarded.accept(condition(*guard));
        if (!test) {
            return guarded;
        }
        if (known_predicate(test) == false) {
            return guarded;
        }
    }
    if (!guarded.continues()) {
        return guarded;
    }
    auto selected = region(source, result);
    if (!returns_result(result) && selected.continues()) {
        selected.terminate(
            generated_statement(
                TargetGotoStmt {.label = done.label, .role = TargetJumpRole::RegionExit}
            ),
            done.target
        );
    }
    if (!known_predicate(test).has_value()) {
        auto branches = std::vector<TargetIfBranch>();
        guarded.record_exits(selected.exits());
        branches.push_back(
            {.condition = predicate_expression(std::move(*test)),
             .body = std::move(selected).finish()}
        );
        guarded.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    } else {
        guarded.append(std::move(selected));
    }
    return guarded;
}

auto BodyRealizer::lower_if(
    const ConstructionConditional& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto lower_branch = [&](this const auto& self,
                                  std::size_t index) noexcept -> LoweringStmtBuilder {
        if (index == value.branches.size()) {
            if (value.otherwise.has_value()) {
                return region(*value.otherwise, result);
            }
            auto completed = LoweringStmtBuilder();
            deliver_result(LoweringCompleted {}, result, completed);
            return completed;
        }
        const auto& branch = value.branches[index];
        auto statements = LoweringStmtBuilder();
        auto test = statements.accept(condition(branch.condition));
        if (!statements.continues()) {
            return statements;
        }
        if (const auto known = known_predicate(test)) {
            if (*known) {
                statements.scope(region(branch.body, result));
            } else {
                statements.append(self(index + 1uz));
            }
            return statements;
        }
        auto branches = std::vector<TargetIfBranch>();
        auto selected = region(branch.body, result);
        auto alternative = self(index + 1uz);
        const auto continues = selected.continues() || alternative.continues();
        statements.record_exits(selected.exits());
        statements.record_exits(alternative.exits());
        branches.push_back(
            {.condition = predicate_expression(std::move(*test)),
             .body = std::move(selected).finish()}
        );
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

auto BodyRealizer::lower_arm(
    const PatternState& pattern,
    std::span<const LocalBindingID> bindings,
    ConstructionRegionID source,
    const std::optional<ConstructionExpressionID>& guard,
    const LoweringResultDestination& result,
    RegionExit& done
) noexcept -> LoweringStmtBuilder {
    auto statements = LoweringStmtBuilder();
    auto chosen = LoweringStmtBuilder();
    for (const auto binding : bindings) {
        chosen.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = true,
                .name = binding_names.at(binding),
                .type = context.lower_type(metadata.binding(binding).type),
                .initializer =
                    dereference_expression(name_expression(pattern.addresses.at(binding)))
            }
        ));
    }
    chosen.append(guarded_region(source, guard, result, done));
    pattern_branch(name_expression(pattern.matched), std::move(chosen), statements);
    return statements;
}

auto BodyRealizer::lower_match(
    const ConstructionMatch& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto done = RegionExit {
        .label = names.fresh(TargetTemporaryNameKind::MatchDone),
        .target = exit_target(LoweringExitKind::Value)
    };
    auto scope = LoweringStmtBuilder();
    const auto subject = names.fresh(TargetTemporaryNameKind::Owner);
    auto subject_value = read_value(expression(value.subject), scope);
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
        auto statements = LoweringStmtBuilder();
        auto matcher = PatternRealizer(context, names, metadata);
        const auto pattern = matcher.prepare(pattern_bindings(arm.bindings), statements);
        matcher.match(
            arm.pattern_id,
            {.root = subject, .dereference_root = false, .payload_index = std::nullopt},
            pattern,
            statements
        );
        statements.append(lower_arm(pattern, arm.bindings, arm.body, arm.guard, result, done));
        scope.scope(std::move(statements));
    }
    if (scope.continues()) {
        scope.terminate(
            generated_statement(
                TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
            ),
            LoweringExitTarget {LoweringExitKind::Unreachable, 0}
        );
    }
    destination.scope(std::move(scope));
    if (destination.exits().contains(done.target)) {
        destination.resume(done.label, TargetJumpRole::RegionExit, done.target);
    }
}

auto BodyRealizer::lower_try(
    ConstructionExpressionID identity,
    const ConstructionTry& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (context.plan().failure_abi().members(value.protected_failures).empty()) {
        destination.append(region(value.body, result));
        return;
    }
    const auto storage = names.fresh(TargetTemporaryNameKind::Try);
    const auto handler = names.fresh(TargetTemporaryNameKind::Try);
    auto done = RegionExit {
        .label = names.fresh(TargetTemporaryNameKind::CatchDone),
        .target = exit_target(LoweringExitKind::Value)
    };
    const auto failures = context.plan().failure_abi().members(value.protected_failures);
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = storage,
            .type = context.optional_type(context.variant_type(failures)),
            .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
        }
    ));
    handlers.emplace(
        identity,
        FailureDestination {
            .storage = storage,
            .label = handler,
            .target = exit_target(LoweringExitKind::Failure)
        }
    );
    auto protected_body = region(value.body, result);
    if (!returns_result(result) && protected_body.continues()) {
        protected_body.terminate(
            generated_statement(
                TargetGotoStmt {.label = done.label, .role = TargetJumpRole::RegionExit}
            ),
            done.target
        );
    }
    destination.scope(std::move(protected_body));
    const auto handler_target = handlers.at(identity).target;
    const auto handler_used = destination.exits().contains(handler_target);
    if (!handler_used) {
        if (destination.exits().contains(done.target)) {
            destination.resume(done.label, TargetJumpRole::RegionExit, done.target);
        }
        return;
    }
    destination.resume(handler, TargetJumpRole::FailureTransfer, handler_target);
    for (const auto& arm : value.arms) {
        if (!destination.continues()) {
            break;
        }
        if (context.plan().failure_abi().members(arm.accepted_failures).empty()) {
            continue;
        }
        auto statements = LoweringStmtBuilder();
        auto matcher = PatternRealizer(context, names, metadata);
        const auto state = matcher.prepare(pattern_bindings(arm.bindings), statements);
        const auto add = [&](TypeID type, std::optional<PatternID> pattern_id) noexcept {
            auto candidate = LoweringStmtBuilder();
            const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
            candidate.emit(generated_statement(
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
            auto selected = LoweringStmtBuilder();
            if (pattern_id) {
                matcher.match(
                    *pattern_id,
                    {.root = projection, .dereference_root = true, .payload_index = std::nullopt},
                    state,
                    selected
                );
            } else {
                selected.emit(generated_statement(
                    TargetAssignmentStmt {
                        .target = name_expression(state.matched),
                        .op = TargetAssignmentOperator::Assign,
                        .value = bool_expression(true)
                    }
                ));
            }
            pattern_branch(name_expression(projection), std::move(selected), candidate);
            pattern_branch(
                prefix_expression(TargetPrefixOperator::LogicalNot, name_expression(state.matched)),
                std::move(candidate),
                statements
            );
        };
        for (const auto& alternative : arm.alternatives) {
            if (const auto* pattern = std::get_if<ConstructionTypedCatch>(&alternative.pattern)) {
                add(pattern->type, pattern->pattern_id);
            } else {
                for (const auto type :
                     context.plan().failure_abi().members(arm.accepted_failures)) {
                    add(type, std::nullopt);
                }
            }
        }
        statements.append(lower_arm(state, arm.bindings, arm.body, arm.guard, result, done));
        destination.scope(std::move(statements));
    }
    transfer_failure(storage, value.residual_failures, value.residual_destination, destination);
    if (destination.exits().contains(done.target)) {
        destination.resume(done.label, TargetJumpRole::RegionExit, done.target);
    }
}

auto BodyRealizer::pattern_bindings(std::span<const LocalBindingID> bindings) const noexcept
    -> std::vector<PatternBindingType> {
    auto result = std::vector<PatternBindingType>();
    result.reserve(bindings.size());
    for (const auto binding : bindings) {
        result.push_back({.binding = binding, .type = metadata.binding(binding).type});
    }
    return result;
}

auto BodyRealizer::pattern_branch(
    TargetExpr condition,
    LoweringStmtBuilder selected,
    LoweringStmtBuilder& destination
) noexcept -> void {
    destination.record_exits(selected.exits());
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({.condition = std::move(condition), .body = std::move(selected).finish()});
    destination.emit(generated_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
}
