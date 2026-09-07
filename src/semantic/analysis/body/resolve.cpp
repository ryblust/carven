module carven:semantic.analysis.body.resolve.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.body.resolve;
import :semantic.semir.traversal;
import :support.visit;
import std;

namespace {

class BodyResolver final {
public:
    BodyResolver(
        const TypeResolution& types,
        const FailureSolution& failures,
        const FailureSetStore& failure_sets,
        CompilationProvenanceReader provenance,
        AnalysisDiagnostics diagnostics
    ) noexcept
        : types(types),
          failures(failures),
          failure_sets(failure_sets),
          provenance(provenance),
          diagnostics(diagnostics) {}

    auto warning(DiagnosticCode code, std::string message, ProgramOriginID origin) const noexcept
        -> void {
        diagnostics.warning(DiagnosticBuilder(code, std::move(message))
                                .primary(provenance.source_span(origin))
                                .build());
    }

    auto operator()(ConstructionTypeRef type) const noexcept -> TypeID {
        return types.resolve(type);
    }

    auto operator()(BodyType& type) const noexcept -> void {
        type = BodyType(types.resolve(type.construction()));
    }

    auto operator()(BodyFailures& term) const noexcept -> void {
        term = BodyFailures(failures.failure_set(term.term()));
    }

    auto operator()(SemanticExpression& value) const noexcept -> void {
        (*this)(value.type);
        (*this)(value.failures);
        if (auto* call = std::get_if<SemCall>(&value.value)) {
            (*this)(call->callee_failures);
        }
        if (auto* attempt = std::get_if<SemTry>(&value.value)) {
            (*this)(*attempt);
        }
    }

    auto operator()(SemanticRegion& value) const noexcept -> void { (*this)(value.failures); }

    auto operator()(SemTry& value) const noexcept -> void {
        const auto protected_failures = failures.failure_set(value.protected_failures.term());
        if (!failure_sets.failure_set(protected_failures).members.empty()) {
            for (auto& arm : value.arms) {
                const auto accepted =
                    failure_sets.failure_set(failures.failure_set(arm.accepted_failures.term()));
                auto useful = false;
                for (auto& alternative : arm.alternatives) {
                    const auto matches = std::visit(
                        Overloaded {
                            [&](CatchAllPattern) noexcept { return !accepted.members.empty(); },
                            [&](const SemTypedCatchPattern& pattern) noexcept {
                                return std::ranges::contains(
                                    accepted.members,
                                    types.resolve(pattern.type.construction())
                                );
                            },
                        },
                        alternative.pattern
                    );
                    alternative.reachable = alternative.reachable && matches;
                    useful = useful || alternative.reachable;
                }
                if (!useful) {
                    warning(
                        DiagnosticCode::EffectCatchArmUnreachable,
                        "catch arm cannot match any remaining protected failure",
                        arm.origin
                    );
                } else {
                    for (const auto& alternative : arm.alternatives) {
                        if (!alternative.reachable) {
                            warning(
                                DiagnosticCode::EffectCatchAlternativeUnreachable,
                                "catch alternative cannot match a remaining protected failure",
                                alternative.origin
                            );
                        }
                    }
                }
            }
        }
        (*this)(value.protected_failures);
        (*this)(value.residual_failures);
        for (auto& arm : value.arms) {
            (*this)(arm.accepted_failures);
            for (auto& alternative : arm.alternatives) {
                if (auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                    (*this)(typed->type);
                }
            }
        }
    }

    auto operator()(ElaboratedLocalBinding&& value) const noexcept -> LocalBinding {
        return {
            .name = value.name,
            .type = (*this)(value.type),
            .lifetime = value.lifetime,
            .storage = value.storage,
            .origin = value.origin,
        };
    }

    auto operator()(ElaboratedPattern&& value) const noexcept -> Pattern {
        return {
            .type = (*this)(value.type),
            .value = std::visit(
                Overloaded {
                    [&](ElaboratedTypeConstraintPattern pattern) noexcept -> PatternValue {
                        return TypeConstraintPattern {.type = (*this)(pattern.type)};
                    },
                    [](auto&& pattern) static noexcept -> PatternValue {
                        return std::forward<decltype(pattern)>(pattern);
                    },
                },
                std::move(value.value)
            ),
            .origin = value.origin,
        };
    }

private:
    const TypeResolution& types;
    const FailureSolution& failures;
    const FailureSetStore& failure_sets;
    CompilationProvenanceReader provenance;
    AnalysisDiagnostics diagnostics;
};

} // namespace

auto resolve_body(
    StructuredBodyDraft body,
    const TypeResolution& types,
    const FailureSolution& failures,
    const FailureSetStore& failure_sets,
    CompilationProvenanceReader provenance,
    AnalysisDiagnostics diagnostics
) noexcept -> SemIRBody {
    const auto resolve = BodyResolver(types, failures, failure_sets, provenance, diagnostics);
    auto bindings = std::move(body.bindings)
                        .transform<LocalBinding>([&](LocalBindingID,
                                                     ElaboratedLocalBinding&& binding) noexcept {
                            return resolve(static_cast<ElaboratedLocalBinding&&>(binding));
                        });
    auto patterns = std::move(body.patterns)
                        .transform<Pattern>([&](PatternID, ElaboratedPattern&& pattern) noexcept {
                            return resolve(std::move(pattern));
                        });
    visit_semantic_nodes(body.region, resolve);
    return SemIRBody({
        .id = body.id,
        .kind = body.kind,
        .provenance_identity = body.provenance_identity,
        .inputs = std::move(body.inputs),
        .lifetime_regions = std::move(body.lifetime_regions),
        .bindings = std::move(bindings).seal(),
        .patterns = std::move(patterns).seal(),
        .region = std::move(body.region),
    });
}
