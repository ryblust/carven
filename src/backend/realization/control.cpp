module carven:backend.realization.control.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :support.invariant;
import :support.visit;
import std;

auto BodyRealizer::structured_expression(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    preparation.operation(source).operation.value.visit(
        Overloaded {
            [&](const SemIf& value) noexcept { lower_if(value, result, destination); },
            [&](const SemMatch& value) noexcept { lower_match(value, result, destination); },
            [&](const SemTry& value) noexcept { lower_try(value, result, destination); },
            [](const auto&) static noexcept {
                invariant_violation("expected a structured semantic expression");
            },
        }
    );
}

auto BodyRealizer::guarded_region(
    const SemanticRegion& source,
    const std::optional<SemanticExpression>& guard,
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
    const SemIf& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto lower_branch = [&](this const auto& self,
                                  std::size_t index) noexcept -> LoweringStmtBuilder {
        if (index == value.branches.size()) {
            if (value.otherwise.has_value()) {
                return region(**value.otherwise, result);
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
    const SemanticRegion& source,
    const std::optional<SemanticExpression>& guard,
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
    const SemMatch& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto done = RegionExit {
        .label = names.fresh(TargetTemporaryNameKind::MatchDone),
        .target = exit_target(LoweringExitKind::Value)
    };
    auto scope = LoweringStmtBuilder();
    const auto subject = names.fresh(TargetTemporaryNameKind::Owner);
    auto subject_value = scope.accept(operand({
        .expression = std::addressof(*value.subject),
        .use = value.subject_is_place ? PreparedUse::ConstPlace : PreparedUse::Consume,
        .demand = PreparedDemand::Value,
    }));
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
        if (!arm.reachable) {
            continue;
        }
        if (!scope.continues()) {
            break;
        }
        auto statements = LoweringStmtBuilder();
        auto matcher = PatternRealizer(
            context,
            names,
            metadata,
            [&](PatternID pattern, bool upper, LoweringStmtBuilder& destination) noexcept {
                return pattern_bound(arm.pattern_bounds, pattern, upper, destination);
            }
        );
        const auto pattern = matcher.prepare(pattern_bindings(arm.bindings), statements);
        matcher.match(
            arm.pattern,
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
    const SemTry& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (context.plan().failure_abi().members(value.protected_failures.resolved()).empty()) {
        destination.append(region(*value.body, result));
        return;
    }
    const auto storage = names.fresh(TargetTemporaryNameKind::Try);
    const auto handler = names.fresh(TargetTemporaryNameKind::Try);
    auto done = RegionExit {
        .label = names.fresh(TargetTemporaryNameKind::CatchDone),
        .target = exit_target(LoweringExitKind::Value)
    };
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
    const auto receiver = FailureDestination {
        .storage = storage,
        .label = handler,
        .target = exit_target(LoweringExitKind::Failure)
    };
    const auto outer = std::exchange(current_failure, receiver);
    auto protected_body = region(*value.body, result);
    current_failure = outer;
    if (!returns_result(result) && protected_body.continues()) {
        protected_body.terminate(
            generated_statement(
                TargetGotoStmt {.label = done.label, .role = TargetJumpRole::RegionExit}
            ),
            done.target
        );
    }
    destination.scope(std::move(protected_body));
    const auto handler_target = receiver.target;
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
        if (context.plan().failure_abi().members(arm.accepted_failures.resolved()).empty()) {
            continue;
        }
        const auto previous_caught = std::exchange(
            caught,
            CaughtFailure {.storage = storage, .failures = arm.accepted_failures.resolved()}
        );
        auto statements = LoweringStmtBuilder();
        auto matcher = PatternRealizer(
            context,
            names,
            metadata,
            [&](PatternID pattern, bool upper, LoweringStmtBuilder& destination) noexcept {
                return pattern_bound(arm.pattern_bounds, pattern, upper, destination);
            }
        );
        const auto state = matcher.prepare(pattern_bindings(arm.bindings), statements);
        const auto add = [&](TypeID type, std::optional<PatternID> pattern_id) noexcept {
            auto candidate = LoweringStmtBuilder();
            const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
            candidate.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::ConstValue,
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
        statements.append(lower_arm(state, arm.bindings, arm.body, arm.guard, result, done));
        caught = previous_caught;
        destination.scope(std::move(statements));
    }
    transfer_failure(storage, value.residual_failures.resolved(), outer, destination);
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

auto BodyRealizer::pattern_bound(
    std::span<const SemPatternBounds> pattern_bounds,
    PatternID pattern,
    bool upper,
    LoweringStmtBuilder& destination
) noexcept -> std::optional<TargetExpr> {
    const auto found = std::ranges::find(pattern_bounds, pattern, &SemPatternBounds::pattern);
    if (found == pattern_bounds.end()) {
        invariant_violation("dynamic range pattern has no expressions");
    }
    const auto& bound = upper ? found->end : found->begin;
    if (!bound) {
        invariant_violation("dynamic range bound has no expression");
    }
    auto value = destination.accept(operand(
        {.expression = std::addressof(*bound),
         .use = PreparedUse::OperandValue,
         .demand = PreparedDemand::Value}
    ));
    if (!destination.continues()) {
        return std::nullopt;
    }
    const auto name = names.fresh(TargetTemporaryNameKind::Operand);
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .maybe_unused = false,
            .name = name,
            .type = context.lower_type(preparation.operation(*bound).operation.type.resolved()),
            .initializer = std::move(*value)
        }
    ));
    return name_expression(name);
}
