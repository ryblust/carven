module carven:semantic.analysis.ownership.facts.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.ownership.context;
import std;

namespace ownership {

auto prepare_body_facts(
    const SemIRBody& body,
    const ProgramDraft& draft,
    std::span<const TypeContents> types
) noexcept -> BodyFacts {
    auto facts = BodyFacts {};
    const auto add = [&](TypeID type, ProgramOriginID origin, LifetimeRegionID lifetime) noexcept {
        const auto index = facts.locals.size();
        facts.locals.push_back({type, origin, lifetime});
        facts.lifetime_objects[lifetime].push_back(index);
        return index;
    };
    for (const auto [id, binding] : body.bindings()) {
        if (id.index() != facts.locals.size()) {
            invariant_violation("local object positions disagree with bindings");
        }
        add(binding.type, binding.origin, binding.lifetime);
    }
    auto prepared_patterns = std::flat_set<PatternID>();
    visit_semantic_nodes(body.region(), [&](const SemanticExpression& expression) noexcept {
        const auto contents = types[expression.type.resolved().index()];
        if (contents.closure_owner || contents.callable_view) {
            facts.temporaries.emplace(
                std::addressof(expression),
                add(expression.type.resolved(), expression.origin, expression.lifetime)
            );
        }
        if (const auto* match = std::get_if<SemMatch>(&expression.value)) {
            for (const auto& arm : match->arms) {
                if (!prepared_patterns.insert(arm.pattern).second) {
                    continue;
                }
                const auto arms = std::array {
                    PatternCoverageArm {.alternatives = {arm.pattern}, .guarded = false}
                };
                auto complete = patterns_exhaustive(
                    draft,
                    body.pattern_table(),
                    body.pattern(arm.pattern).type,
                    arms
                );
                if (!complete) {
                    invariant_violation(complete.error());
                }
                if (*complete) {
                    facts.irrefutable_patterns.insert(arm.pattern);
                }
            }
        }
        const auto* attempt = std::get_if<SemTry>(&expression.value);
        if (attempt == nullptr) {
            return;
        }
        const auto& failures =
            draft.failure_sets().failure_set(attempt->protected_failures.resolved());
        for (const auto& arm : attempt->arms) {
            auto accepted = std::flat_map<TypeID, CatchAcceptance>();
            for (const auto type : failures.members) {
                auto alternatives = std::vector<std::optional<PatternID>>();
                for (const auto& alternative : arm.alternatives) {
                    if (!alternative.reachable) {
                        continue;
                    }
                    if (const auto* typed =
                            std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                        if (typed->type.resolved() == type) {
                            alternatives.push_back(typed->inner);
                        }
                    } else {
                        alternatives.push_back(std::nullopt);
                    }
                }
                if (alternatives.empty()) {
                    continue;
                }
                const auto patterns = std::array {
                    PatternCoverageArm {.alternatives = alternatives, .guarded = false}
                };
                auto complete = patterns_exhaustive(draft, body.pattern_table(), type, patterns);
                if (!complete.has_value()) {
                    invariant_violation(complete.error());
                }
                accepted.emplace(
                    type,
                    CatchAcceptance {
                        .alternatives = std::move(alternatives),
                        .exhaustive = *complete
                    }
                );
            }
            facts.catches.emplace(std::addressof(arm), std::move(accepted));
        }
    });
    return facts;
}

} // namespace ownership
