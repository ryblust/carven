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
import :support.task;
import :support.visit;
import std;

auto BodyRealizer::structured_expression(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    return preparation.operation(source).value.visit(
        Overloaded {
            [&](const SemIf& value) noexcept -> ContinuationTask<std::monostate> {
                return lower_if(value, result, destination);
            },
            [&](const SemMatch& value) noexcept -> ContinuationTask<std::monostate> {
                return lower_match(value, result, destination);
            },
            [&](const SemTry& value) noexcept -> ContinuationTask<std::monostate> {
                return lower_try(value, result, destination);
            },
            [](const auto&) static noexcept -> ContinuationTask<std::monostate> {
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
) noexcept -> ContinuationTask<LoweringStmtBuilder> {
    auto guarded = LoweringStmtBuilder();
    auto test = std::optional<LoweringPredicate>(LoweringKnownBool {true});
    if (guard.has_value()) {
        test = guarded.accept((co_await condition(*guard)));
        if (!test) {
            co_return guarded;
        }
        if (known_predicate(test) == false) {
            co_return guarded;
        }
    }
    if (!guarded.continues()) {
        co_return guarded;
    }
    auto selected = (co_await region(source, result));
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
    co_return guarded;
}

auto BodyRealizer::lower_if(
    const SemIf& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    const auto lower_branch =
        [&](this const auto& self,
            std::size_t index) noexcept -> ContinuationTask<LoweringStmtBuilder> {
        if (index == value.branches.size()) {
            if (value.otherwise.has_value()) {
                co_return (co_await region(**value.otherwise, result));
            }
            auto completed = LoweringStmtBuilder();
            deliver_result(LoweringCompleted {}, result, completed);
            co_return completed;
        }
        const auto& branch = value.branches[index];
        auto statements = LoweringStmtBuilder();
        auto test = statements.accept((co_await condition(branch.condition)));
        if (!statements.continues()) {
            co_return statements;
        }
        if (const auto known = known_predicate(test)) {
            if (*known) {
                statements.scope((co_await region(branch.body, result)));
            } else {
                statements.append((co_await self(index + 1uz)));
            }
            co_return statements;
        }
        auto branches = std::vector<TargetIfBranch>();
        auto selected = (co_await region(branch.body, result));
        auto alternative = (co_await self(index + 1uz));
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
        co_return statements;
    };
    destination.append((co_await lower_branch(0uz)));
    co_return {};
}

auto BodyRealizer::lower_arm(
    const PatternBindings& pattern,
    LoweringPredicate predicate,
    std::span<const LocalBindingID> bindings,
    const SemanticRegion& source,
    const std::optional<SemanticExpression>& guard,
    const LoweringResultDestination& result,
    RegionExit& done
) noexcept -> ContinuationTask<LoweringStmtBuilder> {
    auto statements = LoweringStmtBuilder();
    auto chosen = LoweringStmtBuilder();
    for (const auto binding : bindings) {
        chosen.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = true,
                .local = binding_locals.at(binding),
                .type = context.lower_type(metadata.binding(binding).type),
                .initializer =
                    dereference_expression(name_expression(pattern.addresses.at(binding)))
            }
        ));
    }
    chosen.append((co_await guarded_region(source, guard, result, done)));
    if (const auto* known = std::get_if<LoweringKnownBool>(&predicate)) {
        if (known->value) {
            statements.append(std::move(chosen));
        }
    } else {
        pattern_branch(predicate_expression(std::move(predicate)), std::move(chosen), statements);
    }
    co_return statements;
}

auto BodyRealizer::lower_match(
    const SemMatch& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    auto done = RegionExit {
        .label = names.fresh(TargetTemporaryNameKind::MatchDone),
        .target = exit_target(LoweringExitKind::Value)
    };
    auto scope = LoweringStmtBuilder();
    const auto subject = fresh_local(TargetTemporaryNameKind::Owner);
    auto subject_value = scope.accept((co_await operand({
        .expression = std::addressof(*value.subject),
        .use = value.subject_is_place ? PreparedUse::ConstPlace : PreparedUse::Consume,
        .demand = PreparedDemand::Value,
    })));
    if (!scope.continues()) {
        destination.append(std::move(scope));
        co_return {};
    }
    scope.emit(generated_statement(
        TargetVariableStmt {
            .binding = value.subject_is_place ? TargetVariableBinding::RvalueReference
                                              : TargetVariableBinding::ConstValue,
            .maybe_unused = true,
            .local = subject,
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
        if (const auto* binding =
                std::get_if<BindingPattern>(&metadata.pattern(arm.pattern).value)) {
            declare_binding(binding->binding, name_expression(subject), statements);
            statements.append((co_await guarded_region(arm.body, arm.guard, result, done)));
            scope.scope(std::move(statements));
            continue;
        }
        if (arm.pattern_always_matches) {
            statements.append((co_await guarded_region(arm.body, arm.guard, result, done)));
            scope.scope(std::move(statements));
            continue;
        }
        auto matcher = PatternRealizer(
            context,
            names,
            metadata,
            [&](PatternID pattern, bool upper, LoweringStmtBuilder& destination) noexcept {
                return pattern_bound(arm.pattern_bounds, pattern, upper, destination);
            }
        );
        const auto pattern = matcher.prepare(pattern_bindings(arm.bindings), statements);
        auto predicate = statements.accept(
            co_await matcher.match(
                arm.pattern,
                {.root = subject, .dereference_root = false, .payload_index = std::nullopt},
                pattern
            )
        );
        if (predicate) {
            statements.append(
                co_await lower_arm(
                    pattern,
                    std::move(*predicate),
                    arm.bindings,
                    arm.body,
                    arm.guard,
                    result,
                    done
                )
            );
        }
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
    co_return {};
}

auto BodyRealizer::lower_try(
    const SemTry& value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    if (context.plan().failure_abi().members(value.protected_failures.resolved()).empty()) {
        destination.append((co_await region(*value.body, result)));
        co_return {};
    }
    const auto storage = fresh_local(TargetTemporaryNameKind::Try);
    const auto handler = names.fresh(TargetTemporaryNameKind::Try);
    auto done = RegionExit {
        .label = names.fresh(TargetTemporaryNameKind::CatchDone),
        .target = exit_target(LoweringExitKind::Value)
    };
    const auto failures = context.plan().failure_abi().members(value.protected_failures.resolved());
    const auto slot =
        FailureSlot {.storage = storage, .layout = value.protected_failures.resolved()};
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .local = storage,
            .type = context.optional_type(
                failures.size() == 1uz ? context.lower_type(failures.front())
                                       : context.variant_type(failures)
            ),
            .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
        }
    ));
    const auto receiver = FailureDestination {
        .slot = slot,
        .label = handler,
        .target = exit_target(LoweringExitKind::Failure)
    };
    const auto outer = std::exchange(current_failure, receiver);
    auto protected_body = (co_await region(*value.body, result));
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
        co_return {};
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
            CaughtFailure {.slot = slot, .failures = arm.accepted_failures.resolved()}
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
        auto predicate = std::optional<LoweringPredicate>(LoweringKnownBool {false});
        for (const auto& alternative : arm.alternatives) {
            if (!alternative.reachable) {
                continue;
            }
            if (!predicate || known_predicate(predicate) == true) {
                break;
            }
            auto candidate = LoweringStmtBuilder();
            auto selected = std::optional<LoweringPredicate>(LoweringKnownBool {true});
            if (const auto* pattern = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                if (failures.size() == 1uz) {
                    selected = candidate.accept(
                        co_await matcher.match(
                            pattern->inner,
                            {.root = storage,
                             .dereference_root = true,
                             .payload_index = std::nullopt},
                            state
                        )
                    );
                } else {
                    const auto projection = fresh_local(TargetTemporaryNameKind::FailureProjection);
                    candidate.emit(generated_statement(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::ConstValue,
                            .maybe_unused = false,
                            .local = projection,
                            .type = context.pointer_type(
                                context.intrinsic_type(TargetSymbol::Auto, true)
                            ),
                            .initializer = failure_projection(slot, pattern->type.resolved())
                        }
                    ));
                    selected = matcher.combine(
                        ShortCircuitOperator::And,
                        LoweringDynamicBool {binary_expression(
                            name_expression(projection),
                            TargetBinaryOperator::NotEqual,
                            intrinsic_expression(TargetSymbol::StdNullptr)
                        )},
                        co_await matcher.match(
                            pattern->inner,
                            {.root = projection,
                             .dereference_root = true,
                             .payload_index = std::nullopt},
                            state
                        ),
                        candidate
                    );
                }
            }
            predicate = matcher.combine(
                ShortCircuitOperator::Or,
                std::move(*predicate),
                std::move(candidate).complete<LoweringPredicate>(std::move(selected)),
                statements
            );
        }
        if (predicate) {
            statements.append(
                co_await lower_arm(
                    state,
                    std::move(*predicate),
                    arm.bindings,
                    arm.body,
                    arm.guard,
                    result,
                    done
                )
            );
        }
        caught = previous_caught;
        destination.scope(std::move(statements));
    }
    transfer_failure(slot, value.residual_failures.resolved(), outer, destination);
    if (destination.exits().contains(done.target)) {
        destination.resume(done.label, TargetJumpRole::RegionExit, done.target);
    }
    co_return {};
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
) noexcept -> ContinuationTask<std::optional<TargetExpr>> {
    const auto found = std::ranges::find(pattern_bounds, pattern, &SemPatternBounds::pattern);
    if (found == pattern_bounds.end()) {
        invariant_violation("dynamic range pattern has no expressions");
    }
    const auto& bound = upper ? found->end : found->begin;
    if (!bound) {
        invariant_violation("dynamic range bound has no expression");
    }
    auto value = destination.accept((co_await operand(
        {.expression = std::addressof(*bound),
         .use = PreparedUse::OperandValue,
         .demand = PreparedDemand::Value}
    )));
    if (!destination.continues()) {
        co_return std::nullopt;
    }
    const auto name = fresh_local(TargetTemporaryNameKind::Operand);
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .maybe_unused = false,
            .local = name,
            .type = context.lower_type(preparation.operation(*bound).type.resolved()),
            .initializer = std::move(*value)
        }
    ));
    co_return name_expression(name);
}
