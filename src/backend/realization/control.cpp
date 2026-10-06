module carven:backend.realization.control.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.decl;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :semantic.semir.body;
import :semantic.semir.contents;
import :semantic.semir.ids;
import :semantic.semir.program;
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

class BodyRealizer::FailureSiteQuery final {
public:
    struct Site final {
        TargetStmt* statement;
        FailureEdge* edge;
        bool nested_loop;
    };

    // Edge records and their label storage stay stable through site replacement.
    explicit FailureSiteQuery(std::span<FailureEdge> edges) noexcept;
    auto enter_scope(TargetTraversalScope scope) noexcept -> bool;
    auto leave_scope(TargetTraversalScope scope) noexcept -> bool;
    auto enter_statement(TargetStmt& statement) noexcept -> bool;
    auto finish() && noexcept -> std::vector<Site>;

private:
    std::map<std::string_view, FailureEdge*> index;
    std::vector<Site> sites;
    std::size_t loop_depth = 0uz;
    std::size_t callable_depth = 0uz;
};

BodyRealizer::FailureSiteQuery::FailureSiteQuery(std::span<FailureEdge> edges) noexcept {
    for (auto& edge : edges) {
        index.emplace(edge.label.spelling(), &edge);
    }
}

auto BodyRealizer::FailureSiteQuery::enter_scope(TargetTraversalScope scope) noexcept -> bool {
    if (scope.kind == TargetTraversalScopeKind::Loop) {
        ++loop_depth;
    } else if (scope.kind == TargetTraversalScopeKind::Callable) {
        ++callable_depth;
    }
    return true;
}

auto BodyRealizer::FailureSiteQuery::leave_scope(TargetTraversalScope scope) noexcept -> bool {
    if (scope.kind == TargetTraversalScopeKind::Loop) {
        --loop_depth;
    } else if (scope.kind == TargetTraversalScopeKind::Callable) {
        --callable_depth;
    }
    return true;
}

auto BodyRealizer::FailureSiteQuery::enter_statement(TargetStmt& statement) noexcept -> bool {
    const auto* jump = std::get_if<TargetGotoStmt>(&statement.value);
    if (jump == nullptr || jump->role != TargetJumpRole::FailureTransfer) {
        return true;
    }
    const auto edge = index.find(jump->label.spelling());
    if (edge == index.end()) {
        return true;
    }
    if (callable_depth != 0uz) {
        invariant_violation("failure receiver crosses a target callable boundary");
    }
    sites.push_back(
        {.statement = &statement, .edge = edge->second, .nested_loop = loop_depth != 0uz}
    );
    return true;
}

auto BodyRealizer::FailureSiteQuery::finish() && noexcept -> std::vector<Site> {
    return std::move(sites);
}

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
            chosen,
            true
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
            const auto realize_bound =
                [&](PatternID pattern, bool upper, LoweringStmtBuilder& destination) noexcept {
                    return pattern_bound(arm.pattern_bounds, pattern, upper, destination);
                };
            auto matcher =
                PatternRealizer(context, names, metadata, arm.pattern_bounds, realize_bound);
            auto pattern = co_await matcher.match(
                arm.pattern,
                {.root = subject, .dereference_root = false, .payload_index = std::nullopt},
                !arm.pattern_may_reject
            );
            for (const auto local : matcher.projection_locals()) {
                unused_initializers.emplace(local, UnusedInitializer::Omit);
            }
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
    const auto failures = context.plan().failure_abi().members(value.protected_failures.resolved());
    if (failures.empty()) {
        destination.append((co_await region(*value.body, result)));
        co_return {};
    }
    auto done = RegionExit {
        .label = names.fresh(TargetTemporaryNameKind::CatchDone),
        .target = exit_target(LoweringExitKind::Value)
    };
    const auto receiver = FailureDestination {
        .identity = failure_receivers.size(),
        .target = exit_target(LoweringExitKind::Failure)
    };
    failure_receivers.push_back({.layout = value.protected_failures.resolved(), .edges = {}});
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
    // Own the records before lowering handlers: nested receivers can grow the registry.
    auto edges = std::move(failure_receivers[receiver.identity].edges);

    auto query = FailureSiteQuery(edges);
    protected_body.visit_statements([&](TargetStmt& statement) noexcept {
        if (!traverse_target_statement(statement, query)) {
            invariant_violation("failure site traversal did not complete");
        }
    });
    auto sites = std::move(query).finish();
    if (sites.empty()) {
        if (protected_body.exits().contains(receiver.target)) {
            invariant_violation("failure receiver has an exit without a registered edge");
        }
        destination.scope(std::move(protected_body));
    } else {
        const auto direct =
            sites.size() == 1uz
            && !sites.front().nested_loop
            && !protected_body.exits().crosses_cleanup(receiver.target)
            && std::ranges::all_of(
                context.plan().failure_abi().members(sites.front().edge->failures),
                [&](TypeID type) noexcept {
                    return !failure_contains(sites.front().edge->source, type)
                        || context.semantic().type_contents(type).read_is_value_snapshot();
                }
            );
        if (direct) {
            const auto& site = sites.front();
            auto handler = LoweringStmtBuilder();
            if (site.edge->initializer) {
                const auto& payload = std::get<ValueFailureSource>(site.edge->source);
                handler.declare(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::ConstValue,
                        .maybe_unused = false,
                        .local = payload.storage,
                        .type = context.lower_type(payload.type),
                        .initializer = std::move(*site.edge->initializer)
                    },
                    false
                );
            }
            handler.append((co_await failure_handler(
                value,
                site.edge->source,
                site.edge->failures,
                result,
                done,
                outer
            )));
            protected_body.replace_exit(receiver.target, handler.exits());
            site.statement->value = TargetBlockStmt {.statements = std::move(handler).finish()};
            destination.scope(std::move(protected_body));
        } else {
            const auto slot = FailureSlot {
                .storage = fresh_local(TargetTemporaryNameKind::Try),
                .layout = value.protected_failures.resolved()
            };
            const auto relay = FailureRelay {
                .slot = slot,
                .label = names.fresh(TargetTemporaryNameKind::Try),
                .target = receiver.target
            };
            destination.declare(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .local = slot.storage,
                    .type = context.optional_type(
                        failures.size() == 1uz ? context.lower_type(failures.front())
                                               : context.variant_type(failures)
                    ),
                    .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
                },
                std::ranges::any_of(failures, [&](TypeID type) noexcept {
                    return needs_cleanup(type);
                })
            );
            for (const auto& site : sites) {
                auto transfer = LoweringStmtBuilder();
                if (site.edge->initializer) {
                    deliver_failure(std::move(*site.edge->initializer), relay, transfer);
                } else {
                    transfer = dispatch_failure(site.edge->source, site.edge->failures, relay);
                }
                site.statement->value =
                    TargetBlockStmt {.statements = std::move(transfer).finish()};
            }
            destination.scope(std::move(protected_body));
            destination.resume(relay.label, TargetJumpRole::FailureTransfer, receiver.target);
            destination.append((co_await failure_handler(
                value,
                slot,
                value.protected_failures.resolved(),
                result,
                done,
                outer
            )));
        }
    }
    if (destination.exits().contains(done.target)) {
        destination.resume(done.label, TargetJumpRole::RegionExit, done.target);
    }
    co_return {};
}

auto BodyRealizer::failure_handler(
    const SemTry& value,
    const FailureSource& source,
    FailureSetID failures,
    const LoweringResultDestination& result,
    RegionExit& done,
    const std::optional<FailureDestination>& outer
) noexcept -> ContinuationTask<LoweringStmtBuilder> {
    auto handler = LoweringStmtBuilder();
    const auto candidates = context.plan().failure_abi().members(failures);
    const auto single = std::ranges::count_if(
                            candidates,
                            [&](TypeID type) noexcept { return failure_contains(source, type); }
                        )
        == 1;
    for (const auto& arm : value.arms) {
        if (!handler.continues()) {
            break;
        }
        if (!std::ranges::any_of(
                context.plan().failure_abi().members(arm.accepted_failures.resolved()),
                [&](TypeID type) noexcept {
                    return std::ranges::contains(candidates, type)
                        && failure_contains(source, type);
                }
            )) {
            continue;
        }
        const auto previous_caught = std::exchange(
            caught,
            CaughtFailure {.source = source, .failures = arm.accepted_failures.resolved()}
        );
        auto statements = LoweringStmtBuilder();
        const auto realize_bound =
            [&](PatternID pattern, bool upper, LoweringStmtBuilder& destination) noexcept {
                return pattern_bound(arm.pattern_bounds, pattern, upper, destination);
            };
        auto matcher = PatternRealizer(context, names, metadata, arm.pattern_bounds, realize_bound);
        auto choices = std::vector<PatternSelection>();
        for (const auto& alternative : arm.alternatives) {
            if (!alternative.reachable) {
                continue;
            }
            auto selected = matcher.test(
                std::move(LoweringStmtBuilder())
                    .complete<LoweringPredicate>(LoweringKnownBool {true})
            );
            if (const auto* pattern = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                const auto type = pattern->type.resolved();
                if (!std::ranges::contains(candidates, type) || !failure_contains(source, type)) {
                    continue;
                }
                const auto direct_subject = source.visit(
                    Overloaded {
                        [](const ValueFailureSource& payload) static noexcept
                            -> std::optional<PatternSubject> {
                            return PatternSubject {
                                .root = payload.storage,
                                .dereference_root = false,
                                .payload_index = std::nullopt
                            };
                        },
                        [&](const FailureSlot& slot) noexcept -> std::optional<PatternSubject> {
                            if (context.plan().failure_abi().members(slot.layout).size() == 1uz) {
                                return PatternSubject {
                                    .root = slot.storage,
                                    .dereference_root = true,
                                    .payload_index = std::nullopt
                                };
                            }
                            return std::nullopt;
                        },
                        [](const OutcomeFailureSource&) static noexcept
                            -> std::optional<PatternSubject> { return std::nullopt; },
                    }
                );
                if (direct_subject) {
                    selected = co_await matcher.match(pattern->inner, *direct_subject);
                } else {
                    const auto projection = fresh_local(TargetTemporaryNameKind::FailureProjection);
                    unused_initializers.emplace(projection, UnusedInitializer::Omit);
                    auto candidate = LoweringStmtBuilder();
                    candidate.declare(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::ConstValue,
                            .maybe_unused = false,
                            .local = projection,
                            .type = context.pointer_type(
                                context.intrinsic_type(TargetSymbol::Auto, true)
                            ),
                            .initializer = failure_projection(source, type)
                        },
                        false
                    );
                    selected = single ? matcher.test(
                                            std::move(candidate).complete<LoweringPredicate>(
                                                LoweringKnownBool {true}
                                            )
                                        )
                                      : matcher.test(
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
        for (const auto local : matcher.projection_locals()) {
            unused_initializers.emplace(local, UnusedInitializer::Omit);
        }
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
        handler.scope(std::move(statements));
    }
    transfer_failure(source, value.residual_failures.resolved(), outer, handler);
    co_return handler;
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
