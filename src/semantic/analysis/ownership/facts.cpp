module carven:semantic.analysis.ownership.facts.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.ownership.context;
import std;

namespace {

auto prepare_ownership_body_facts(
    const SemIRBody& body,
    const SemIRProgram& program,
    OwnershipRecursionBuilder& recursion
) noexcept -> OwnershipBodyFacts {
    auto facts = OwnershipBodyFacts {};
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
    recursion.begin_body(body.id());
    visit_semantic_nodes(body.region(), [&](const SemanticExpression& expression) noexcept {
        recursion.observe(expression);
        const auto contents = program.type_contents(expression.type.resolved());
        if (!expression.selects_storage()
            && (contents.contains_operation_owner
                || contents.contains_closure_owner
                || contents.contains_callable_view
                || contents.contains_storage_owner)) {
            facts.temporaries.emplace(
                std::addressof(expression),
                add(expression.type.resolved(), expression.origin, expression.lifetime)
            );
        }
        const auto* attempt = std::get_if<SemTry>(&expression.value);
        if (attempt == nullptr) {
            return;
        }
        const auto& failures =
            program.failure_sets().failure_set(attempt->protected_failures.resolved());
        for (const auto& arm : attempt->arms) {
            auto accepted = std::flat_map<TypeID, OwnershipCatchAcceptance>();
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
                auto complete = patterns_exhaustive(program, body.pattern_table(), type, patterns);
                if (!complete.has_value()) {
                    invariant_violation(complete.error());
                }
                accepted.emplace(
                    type,
                    OwnershipCatchAcceptance {
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

} // namespace

auto prepare_ownership_analysis(const SemIRProgram& program) noexcept -> OwnershipPreparation {
    auto result = OwnershipPreparation {};
    auto recursion = OwnershipRecursionBuilder(program);
    for (const auto [id, body] : program.bodies().entries()) {
        result.body_facts.emplace(id, prepare_ownership_body_facts(body, program, recursion));
    }
    result.recursion_components = std::move(recursion).finish();
    return result;
}
