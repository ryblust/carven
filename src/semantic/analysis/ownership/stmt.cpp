module carven:semantic.analysis.ownership.stmt.impl;

import :semantic.analysis.ownership.context;
import std;

auto OwnershipBodyAnalyzer::complete_expression(
    const SemanticExpression& source,
    OwnershipState state
) noexcept -> OwnershipFlow {
    const auto previous = full_expression;
    const auto owns = full_expression != source.lifetime
        && body.lifetime_regions().region(source.lifetime).kind
            == LifetimeRegionKind::FullExpression;
    if (owns) {
        full_expression = source.lifetime;
    }
    auto result = expression(source, std::move(state));
    if (owns) {
        leave(result, source.lifetime);
        full_expression = previous;
    }
    return result;
}

auto OwnershipBodyAnalyzer::region(
    const SemanticRegion& source,
    OwnershipState state,
    bool release
) noexcept -> OwnershipFlow {
    auto result = OwnershipFlow {.normal = std::move(state), .value = {}, .exits = {}};
    for (const auto& item : source.statements) {
        if (!result.normal.has_value()) {
            break;
        }
        auto next = statement(item, std::move(*result.normal));
        result.normal = std::move(next.normal);
        append_ownership_exits(result, next);
    }
    if (result.normal.has_value() && source.result.has_value()) {
        auto next = expression(*source.result, std::move(*result.normal));
        result.normal = std::move(next.normal);
        result.value = std::move(next.value);
        append_ownership_exits(result, next);
    }
    if (release) {
        leave(result, source.lifetime);
    }
    return result;
}

auto OwnershipBodyAnalyzer::statement(
    const SemanticStatement& source,
    OwnershipState state
) noexcept -> OwnershipFlow {
    const auto previous_full_expression = full_expression;
    const auto owns =
        body.lifetime_regions().region(source.lifetime).kind == LifetimeRegionKind::FullExpression
        && full_expression != source.lifetime;
    if (owns) {
        full_expression = source.lifetime;
    }
    auto result = OwnershipFlow {.normal = std::move(state), .value = {}, .exits = {}};
    const auto evaluate = [&](const SemanticExpression& expression_source) noexcept {
        if (result.normal.has_value()) {
            auto next = expression(expression_source, std::move(*result.normal));
            result.normal = std::move(next.normal);
            result.value = std::move(next.value);
            append_ownership_exits(result, next);
        }
    };
    const auto transfer = [&](OwnershipExitKind kind,
                              std::optional<TypeID> failure = std::nullopt) noexcept {
        if (result.normal.has_value()) {
            result.exits.push_back(
                {kind,
                 failure,
                 std::move(*result.normal),
                 kind == OwnershipExitKind::Return ? std::move(result.value)
                                                   : OwnershipRelationships {}}
            );
            result.normal.reset();
        }
    };
    std::visit(
        Overloaded {
            [&](const SemReturn& value) noexcept {
                if (value.value.has_value()) {
                    evaluate(*value.value);
                }
                transfer(OwnershipExitKind::Return);
            },
            [&](const SemBreak&) noexcept { transfer(OwnershipExitKind::Break); },
            [&](const SemContinue&) noexcept { transfer(OwnershipExitKind::Continue); },
            [&](const SemRethrow&) noexcept {
                for (const auto type : caught) {
                    result.exits.push_back({OwnershipExitKind::Failure, type, *result.normal, {}});
                }
                result.normal.reset();
            },
            [&](const SemThrow& value) noexcept {
                evaluate(value.value);
                transfer(OwnershipExitKind::Failure, value.failure_type);
            },
            [&](const SemExpressionStatement& value) noexcept { evaluate(value.expression); },
            [&](const SemInitialize& value) noexcept {
                evaluate(value.initializer);
                if (result.normal.has_value()) {
                    store(
                        *result.normal,
                        binding_place(value.binding),
                        result.value,
                        source.origin
                    );
                }
            },
            [&](const SemAssign& value) noexcept {
                const auto target = *location(value.target);
                const auto whole = target.path.empty();
                result = place(
                    value.target,
                    std::move(*result.normal),
                    !whole || value.compound.has_value()
                );
                if (!result.normal.has_value()) {
                    return;
                }
                write_access(target, value.target.origin);
                const auto previous = accesses.size();
                if (!whole || value.compound.has_value()) {
                    accesses.push_back({target, false});
                }
                evaluate(value.value);
                accesses.resize(previous);
                if (result.normal.has_value()) {
                    store(*result.normal, target, result.value, source.origin);
                }
            },
            [&](const SemLoop& value) noexcept { result = loop(value, std::move(*result.normal)); },
            [&](const SemRangeLoop& value) noexcept {
                result = range(value, std::move(*result.normal));
            },
            [&](const SemTestReport& value) noexcept {
                if (value.condition.has_value()) {
                    evaluate(*value.condition);
                }
                if (value.message.has_value()) {
                    evaluate(*value.message);
                }
                const auto known = value.condition.has_value() ? constant_truth(*value.condition)
                                                               : std::optional(false);
                if (value.kind == TestReportKind::Fail
                    || (value.kind == TestReportKind::Require && known == false)) {
                    result.normal.reset();
                }
            },
            [&](const OwnedSemanticRegion& value) noexcept {
                result = region(*value, std::move(*result.normal));
            },
        },
        source.value
    );
    if (owns) {
        leave(result, source.lifetime);
        full_expression = previous_full_expression;
    }
    return result;
}
