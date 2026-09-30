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

namespace {

// Selects between one branch and its alternative; emission spells a nested
// conditional alternative as `else if`.
auto conditional(TargetIfBranch branch, std::vector<TargetStmt> alternative) noexcept
    -> TargetStmt {
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back(std::move(branch));
    return generated_statement(
        TargetIfStmt {
            .branches = std::move(branches),
            .else_body = alternative.empty() ? std::nullopt : std::optional(std::move(alternative))
        }
    );
}

} // namespace

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
        auto selected = (co_await region(branch.body, result));
        auto alternative = (co_await self(index + 1uz));
        const auto continues = selected.continues() || alternative.continues();
        statements.record_exits(selected.exits());
        statements.record_exits(alternative.exits());
        statements.emit(
            conditional(
                {.condition = predicate_expression(std::move(*test)),
                 .body = std::move(selected).finish()},
                std::move(alternative).finish()
            ),
            continues
        );
        co_return statements;
    };
    destination.append((co_await lower_branch(0uz)));
    co_return {};
}

auto BodyRealizer::lower_arm(
    const PatternSelection& pattern,
    std::span<const LocalBindingID> bindings,
    const SemanticRegion& source,
    const std::optional<SemanticExpression>& guard,
    const LoweringResultDestination& result,
    RegionExit* done
) noexcept -> ContinuationTask<LoweringStmtBuilder> {
    auto chosen = LoweringStmtBuilder();
    if (!pattern.accepted) {
        co_return chosen;
    }
    for (const auto binding : bindings) {
        declare_binding(
            binding,
            PatternRealizer::subject_expression(pattern.bindings.at(binding)),
            chosen
        );
    }
    if (done != nullptr) {
        chosen.append((co_await guarded_region(source, guard, result, *done)));
    } else {
        if (guard) {
            invariant_violation("guarded pattern arm has no selection exit");
        }
        chosen.append((co_await region(source, result)));
    }
    co_return chosen;
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
    // A named local subject is matched in place; any other subject is held once.
    const auto* named = std::get_if<SemBinding>(&value.subject->value);
    const auto direct = value.subject_is_place
        && named != nullptr
        && !capture_names.contains(named->binding)
        && !delayed_bindings.contains(named->binding);
    const auto subject =
        direct ? binding_locals.at(named->binding) : fresh_local(TargetTemporaryNameKind::Owner);
    if (!direct) {
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
    }
    // Arms are tried in order. A guard-free arm whose test needs no statements
    // selects between its body and the remaining arms, so the chain needs no
    // exit label; other arms fall through to their successors and leave through
    // `done` when selected.
    const auto lower_arms =
        [&](this const auto& self,
            std::size_t first) noexcept -> ContinuationTask<LoweringStmtBuilder> {
        auto arms = LoweringStmtBuilder();
        for (auto index = first; index < value.arms.size() && arms.continues(); ++index) {
            const auto& arm = value.arms[index];
            if (!arm.reachable) {
                continue;
            }
            auto matcher = PatternRealizer(
                context,
                names,
                metadata,
                arm.pattern_bounds,
                [&](PatternID pattern, bool upper, LoweringStmtBuilder& destination) noexcept {
                    return pattern_bound(arm.pattern_bounds, pattern, upper, destination);
                }
            );
            auto pattern = co_await matcher.match(
                arm.pattern,
                {.root = subject, .dereference_root = false, .payload_index = std::nullopt}
            );
            removable_locals.insert_range(matcher.projection_locals());
            if (!arm.guard
                && pattern.tests.size() == 1uz
                && pattern.tests.front().statements.empty()
                && pattern.tests.front().normal
                && !known_predicate(pattern.tests.front().normal).has_value()) {
                auto predicate = std::move(*pattern.tests.front().normal);
                pattern.tests.clear();
                auto selected =
                    co_await lower_arm(pattern, arm.bindings, arm.body, arm.guard, result, nullptr);
                auto alternative = (co_await self(index + 1uz));
                const auto continues = selected.continues() || alternative.continues();
                arms.record_exits(selected.exits());
                arms.record_exits(alternative.exits());
                arms.emit(
                    conditional(
                        {.condition = predicate_expression(std::move(predicate)),
                         .body = std::move(selected).finish()},
                        std::move(alternative).finish()
                    ),
                    continues
                );
                co_return arms;
            }
            const auto unconditional = !pattern.rejected && !arm.guard;
            auto selected = co_await lower_arm(
                pattern,
                arm.bindings,
                arm.body,
                arm.guard,
                result,
                unconditional ? nullptr : std::addressof(done)
            );
            arms.scope(matcher.select(std::move(pattern), std::move(selected)));
            if (unconditional) {
                co_return arms;
            }
        }
        if (arms.continues()) {
            arms.terminate(
                generated_statement(
                    TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
                ),
                LoweringExitTarget {LoweringExitKind::Unreachable, 0}
            );
        }
        co_return arms;
    };
    if (scope.continues()) {
        scope.append((co_await lower_arms(0uz)));
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
            arm.pattern_bounds,
            [&](PatternID pattern, bool upper, LoweringStmtBuilder& destination) noexcept {
                return pattern_bound(arm.pattern_bounds, pattern, upper, destination);
            }
        );
        auto choices = std::vector<PatternSelection>();
        for (const auto& alternative : arm.alternatives) {
            if (!alternative.reachable) {
                continue;
            }
            auto candidate = LoweringStmtBuilder();
            auto selected = matcher.test(
                std::move(LoweringStmtBuilder())
                    .complete<LoweringPredicate>(LoweringKnownBool {true})
            );
            if (const auto* pattern = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                if (failures.size() == 1uz) {
                    selected = co_await matcher.match(
                        pattern->inner,
                        {.root = storage, .dereference_root = true, .payload_index = std::nullopt}
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
                    selected = matcher.test(
                        std::move(candidate).complete<LoweringPredicate>(
                            LoweringDynamicBool {binary_expression(
                                name_expression(projection),
                                TargetBinaryOperator::NotEqual,
                                intrinsic_expression(TargetSymbol::StdNullptr)
                            )}
                        )
                    );
                    selected.source_locals.insert(projection);
                    selected = matcher.sequence(
                        std::move(selected),
                        co_await matcher.match(
                            pattern->inner,
                            {.root = projection,
                             .dereference_root = true,
                             .payload_index = std::nullopt}
                        )
                    );
                }
            }
            const auto rejected = selected.rejected;
            choices.push_back(std::move(selected));
            if (!rejected) {
                break;
            }
        }
        auto pattern = matcher.alternatives(std::move(choices));
        removable_locals.insert_range(matcher.projection_locals());
        auto selected = co_await lower_arm(
            pattern,
            arm.bindings,
            arm.body,
            arm.guard,
            result,
            std::addressof(done)
        );
        statements.append(matcher.select(std::move(pattern), std::move(selected)));
        caught = previous_caught;
        destination.scope(std::move(statements));
    }
    transfer_failure(slot, value.residual_failures.resolved(), outer, destination);
    if (destination.exits().contains(done.target)) {
        destination.resume(done.label, TargetJumpRole::RegionExit, done.target);
    }
    co_return {};
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
