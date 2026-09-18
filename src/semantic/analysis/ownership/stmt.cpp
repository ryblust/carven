module carven:semantic.analysis.ownership.stmt.impl;

import :semantic.analysis.ownership.context;
import std;

auto OwnershipBodyAnalyzer::complete_expression(
    const SemanticExpression& source,
    OwnershipState state
) noexcept -> ContinuationTask<OwnershipFlow> {
    const auto previous = full_expression;
    const auto owns = full_expression != source.lifetime
        && body.lifetime_regions().region(source.lifetime).kind
            == LifetimeRegionKind::FullExpression;
    if (owns) {
        full_expression = source.lifetime;
    }
    auto result = (co_await expression(source, std::move(state)));
    if (result.normal) {
        result.normal->value = {};
    }
    if (owns) {
        leave(result, source.lifetime);
        full_expression = previous;
    }
    co_return result;
}

auto OwnershipBodyAnalyzer::region(
    const SemanticRegion& source,
    OwnershipState state,
    bool release
) noexcept -> ContinuationTask<OwnershipFlow> {
    auto result = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}, {}}, .exits = {}};
    for (const auto& item : source.statements) {
        if (!result.normal.has_value()) {
            break;
        }
        auto next = (co_await statement(item, std::move(result.normal->state)));
        result.normal = std::move(next.normal);
        append_ownership_exits(result, next);
    }
    if (result.normal.has_value() && source.result.has_value()) {
        auto next = (co_await expression(*source.result, std::move(result.normal->state)));
        result.normal = std::move(next.normal);

        append_ownership_exits(result, next);
    }
    if (release) {
        leave(result, source.lifetime);
    }
    co_return result;
}

auto OwnershipBodyAnalyzer::statement(
    const SemanticStatement& source,
    OwnershipState state
) noexcept -> ContinuationTask<OwnershipFlow> {
    const auto previous_full_expression = full_expression;
    const auto owns =
        body.lifetime_regions().region(source.lifetime).kind == LifetimeRegionKind::FullExpression
        && full_expression != source.lifetime;
    if (owns) {
        full_expression = source.lifetime;
    }
    auto result = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}, {}}, .exits = {}};
    const auto evaluate = [&](
                              const SemanticExpression& expression_source
                          ) noexcept -> ContinuationTask<std::monostate> {
        if (result.normal.has_value()) {
            auto next = (co_await expression(expression_source, std::move(result.normal->state)));
            result.normal = std::move(next.normal);

            append_ownership_exits(result, next);
        }
        co_return {};
    };
    const auto transfer = [&](auto payload) noexcept {
        if (result.normal) {
            if constexpr (std::same_as<decltype(payload), OwnershipReturn>
                          || std::same_as<decltype(payload), OwnershipFailure>) {
                payload.value = std::move(result.normal->value);
            }
            result.exits.push_back({std::move(payload), std::move(result.normal->state)});
            result.normal.reset();
        }
    };
    (co_await source.value.visit(
        Overloaded {
            [&](const SemReturn& value) noexcept -> ContinuationTask<std::monostate> {
                if (value.value.has_value()) {
                    (co_await evaluate(*value.value));
                }
                transfer(OwnershipReturn {});
                co_return {};
            },
            [&](const SemBreak&) noexcept -> ContinuationTask<std::monostate> {
                transfer(OwnershipBreak {});
                co_return {};
            },
            [&](const SemContinue&) noexcept -> ContinuationTask<std::monostate> {
                transfer(OwnershipContinue {});
                co_return {};
            },
            [&](const SemRethrow&) noexcept -> ContinuationTask<std::monostate> {
                if (!caught) {
                    invariant_violation("rethrow has no active failure payload");
                }
                result.exits.push_back({*caught, std::move(result.normal->state)});
                result.normal.reset();
                co_return {};
            },
            [&](const SemThrow& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.value));
                transfer(OwnershipFailure {value.failure_type, {}});
                co_return {};
            },
            [&](const SemExpressionStatement& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.expression));
                co_return {};
            },
            [&](const SemInitialize& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.initializer));
                if (result.normal.has_value()) {
                    store(
                        result.normal->state,
                        binding_place(value.binding),
                        result.normal->value,
                        source.origin
                    );
                }
                co_return {};
            },
            [&](const SemAssign& value) noexcept -> ContinuationTask<std::monostate> {
                result = (co_await place(value.target, std::move(result.normal->state), false));
                if (!result.normal) {
                    co_return {};
                }
                const auto targets = result.normal->storage;
                const auto previous = accesses.size();
                for (const auto& target : targets) {
                    write_access(target, value.target.origin);
                    if (!target.path.empty() || value.compound) {
                        require_available(result.normal->state, target, value.target.origin);
                        accesses.push_back({target, false});
                    }
                }
                (co_await evaluate(value.value));
                accesses.resize(previous);
                if (result.normal) {
                    if (targets.empty()
                        && tracked_borrows(result.normal->value, result.normal->state)) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "tracked borrows cannot be stored through a ptr",
                            source.origin
                        );
                    }
                    for (const auto& target : targets) {
                        store(
                            result.normal->state,
                            target,
                            result.normal->value,
                            source.origin,
                            targets.size() == 1uz
                        );
                    }
                }
                co_return {};
            },
            [&](const SemLoop& value) noexcept -> ContinuationTask<std::monostate> {
                result = (co_await loop(value, std::move(result.normal->state)));
                co_return {};
            },
            [&](const SemRangeLoop& value) noexcept -> ContinuationTask<std::monostate> {
                result = (co_await range(value, std::move(result.normal->state)));
                co_return {};
            },
            [&](const OwnedSemanticRegion& value) noexcept -> ContinuationTask<std::monostate> {
                result = (co_await region(*value, std::move(result.normal->state)));
                co_return {};
            },
        }
    ));
    if (result.normal) {
        result.normal->value = {};
    }
    if (owns) {
        leave(result, source.lifetime);
        full_expression = previous_full_expression;
    }
    co_return result;
}
