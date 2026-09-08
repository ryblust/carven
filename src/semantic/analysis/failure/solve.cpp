module carven:semantic.analysis.failure.solve.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.failure;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto merge_members(std::vector<TypeID>& destination, std::span<const TypeID> source) noexcept
    -> bool {
    auto merged = std::vector<TypeID>();
    merged.reserve(destination.size() + source.size());
    std::ranges::set_union(
        destination,
        source,
        std::back_inserter(merged),
        {},
        &TypeID::index,
        &TypeID::index
    );
    if (merged == destination) {
        return false;
    }
    destination = std::move(merged);
    return true;
}

auto remove_members(std::vector<TypeID>& destination, std::span<const TypeID> excluded) noexcept
    -> void {
    auto retained = std::vector<TypeID>();
    retained.reserve(destination.size());
    std::ranges::set_difference(
        destination,
        excluded,
        std::back_inserter(retained),
        {},
        &TypeID::index,
        &TypeID::index
    );
    destination = std::move(retained);
}

auto retain_members(std::vector<TypeID>& destination, std::span<const TypeID> retained) noexcept
    -> void {
    auto intersection = std::vector<TypeID>();
    intersection.reserve(std::min(destination.size(), retained.size()));
    std::ranges::set_intersection(
        destination,
        retained,
        std::back_inserter(intersection),
        {},
        &TypeID::index,
        &TypeID::index
    );
    destination = std::move(intersection);
}

auto subset(std::span<const TypeID> actual, std::span<const TypeID> allowed) noexcept -> bool {
    return std::ranges::includes(allowed, actual, {}, &TypeID::index, &TypeID::index);
}

auto empty_requirement_diagnostic(
    const RequiresEmptyFailure& requirement,
    CompilationProvenanceReader provenance,
    std::span<const TypeID> members,
    const FailureTypeDiagnosticNames& type_names
) noexcept -> Diagnostic {
    auto code = DiagnosticCode::EffectUnmarked;
    auto message = std::string("failure-producing expression requires explicit '?' propagation");
    switch (requirement.kind) {
        case EmptyFailureRequirementKind::OrdinaryConsumption: break;
        case EmptyFailureRequirementKind::CatchResidual:
            code = DiagnosticCode::EffectCatchNonExhaustive;
            message = "catch does not cover every protected failure";
            break;
        case EmptyFailureRequirementKind::RootBoundary:
            code = DiagnosticCode::EffectRootUnhandled;
            message = "effect root leaves failures unhandled";
            break;
    }
    auto diagnostic = DiagnosticBuilder(code, std::move(message));
    diagnostic.primary(provenance.source_span(requirement.origin));
    if (requirement.kind == EmptyFailureRequirementKind::CatchResidual) {
        auto names = std::vector<std::string>();
        names.reserve(members.size());
        for (const auto member : members) {
            names.push_back(type_names.name(member));
        }
        std::ranges::sort(names);
        for (const auto& name : names) {
            diagnostic.note(std::format("failure type not fully covered: {}", name));
        }
    }
    return diagnostic.build();
}

auto subset_diagnostic(
    const RequiresFailureSubset& requirement,
    CompilationProvenanceReader provenance
) noexcept -> Diagnostic {
    switch (requirement.kind) {
        case FailureSubsetRequirementKind::DeclaredCallable:
            return DiagnosticBuilder(
                       DiagnosticCode::EffectSignatureBound,
                       "callable body exceeds its declared failure contract"
            )
                .primary(provenance.source_span(requirement.origin))
                .build();
        case FailureSubsetRequirementKind::CallableAdoption:
            return DiagnosticBuilder(
                       DiagnosticCode::TypeMismatch,
                       "callable source failures exceed the target callable view"
            )
                .primary(provenance.source_span(requirement.origin))
                .build();
    }
    std::unreachable();
}

} // namespace

FailureTypeDiagnosticNames::FailureTypeDiagnosticNames(
    const CanonicalTypeStoreBuilder& source_types,
    DeclarationConstructionView source_declarations,
    CompilationProvenanceReader source_provenance
) noexcept
    : types(source_types),
      declarations(source_declarations),
      provenance(source_provenance) {
    if (types.owner() != declarations.owner()) {
        invariant_violation("failure diagnostic names belong to different semantic programs");
    }
}

auto FailureTypeDiagnosticNames::name(TypeID type) const noexcept -> std::string {
    const auto qualify = [&](const auto& declaration) noexcept {
        const auto module_decl = declarations.module_decl(declaration.module_id);
        return std::format(
            "{}.{}",
            provenance.module_path_copy(module_decl.provenance_module).value(),
            provenance.spelling_copy(declaration.name)
        );
    };
    const auto canonical = types.copy(type);
    if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
        return qualify(declarations.structure(structure->structure));
    }
    if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value)) {
        return qualify(declarations.enumeration(enumeration->enumeration));
    }
    invariant_violation("failure diagnostic requires a nominal type");
}

FailureSolution::FailureSolution(
    ProgramIdentity identity,
    std::vector<FailureSetID> solutions
) noexcept
    : program_identity(identity),
      failure_sets_by_term(std::move(solutions)) {}

auto FailureSolution::owner() const noexcept -> ProgramIdentity {
    return program_identity;
}

auto FailureSolution::contains(FailureTermID term) const noexcept -> bool {
    return term.owner() == program_identity
        && static_cast<std::size_t>(term.index()) < failure_sets_by_term.size();
}

auto FailureSolution::failure_set(FailureTermID term) const noexcept -> FailureSetID {
    if (!contains(term)) {
        invariant_violation("failure solution lookup used a foreign or invalid term identity");
    }
    return failure_sets_by_term[term.index()];
}

auto solve_failure_constraints(
    FrozenFailureConstraints&& constraints,
    FailureSetStoreBuilder& failure_sets,
    CompilationProvenanceReader provenance,
    const FailureTypeDiagnosticNames& type_names,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<FailureSolution> {
    if (constraints.owner() != failure_sets.owner()) {
        invariant_violation("failure solver inputs belong to different semantic programs");
    }
    if (constraints.provenance_owner() != provenance.identity()) {
        invariant_violation("failure solver received a foreign provenance view");
    }

    const auto& terms = constraints.terms();
    auto values = std::vector<std::vector<TypeID>>(terms.size());
    auto dependents = std::vector<std::vector<std::uint32_t>>(terms.size());
    auto term_ids = std::vector<FailureTermID>();
    term_ids.reserve(terms.size());
    for (const auto [term_id, term] : terms.entries()) {
        term_ids.push_back(term_id);
        values[term_id.index()] = term.direct_members;
        remove_members(values[term_id.index()], term.excluded_members);
        if (term.retained_members.has_value()) {
            retain_members(values[term_id.index()], *term.retained_members);
        }
        for (const auto input : term.inputs) {
            if (!terms.contains(input)) {
                invariant_violation("failure term depends on a foreign or invalid term");
            }
            dependents[input.index()].push_back(term_id.index());
        }
        for (const auto& guarded : term.guarded_inputs) {
            if (!terms.contains(guarded.gate) || !terms.contains(guarded.source)) {
                invariant_violation("guarded failure contribution depends on an invalid term");
            }
            dependents[guarded.gate.index()].push_back(term_id.index());
            dependents[guarded.source.index()].push_back(term_id.index());
        }
    }
    for (auto& targets : dependents) {
        std::ranges::sort(targets);
        targets.erase(std::ranges::unique(targets).begin(), targets.end());
    }

    auto worklist = std::deque<std::uint32_t>();
    auto queued = std::vector<std::uint8_t>(terms.size(), 1u);
    for (auto index = 0uz; index < terms.size(); ++index) {
        worklist.push_back(static_cast<std::uint32_t>(index));
    }
    while (!worklist.empty()) {
        const auto index = worklist.front();
        worklist.pop_front();
        queued[index] = 0u;

        const auto term_id = term_ids[index];
        const auto& term = terms.get(term_id);
        auto next = term.direct_members;
        for (const auto input : term.inputs) {
            static_cast<void>(merge_members(next, values[input.index()]));
        }
        for (const auto& guarded : term.guarded_inputs) {
            if (!values[guarded.gate.index()].empty()) {
                static_cast<void>(merge_members(next, values[guarded.source.index()]));
            }
        }
        remove_members(next, term.excluded_members);
        if (term.retained_members.has_value()) {
            retain_members(next, *term.retained_members);
        }
        if (!subset(values[index], next)) {
            invariant_violation("failure fixed-point transfer was not monotone");
        }
        if (next == values[index]) {
            continue;
        }
        values[index] = std::move(next);
        for (const auto dependent : dependents[index]) {
            if (queued[dependent] == 0u) {
                queued[dependent] = 1u;
                worklist.push_back(dependent);
            }
        }
    }

    auto failure_ids = std::vector<FailureSetID>();
    failure_ids.reserve(values.size());
    for (const auto& concrete_members : values) {
        failure_ids.push_back(failure_sets.intern(concrete_members));
    }

    auto failure = std::optional<AnalysisFailure>();
    const auto members = [&](FailureTermID term) noexcept -> std::span<const TypeID> {
        if (!terms.contains(term)) {
            invariant_violation("failure constraint refers to a foreign or invalid term");
        }
        return values[term.index()];
    };
    for (const auto& constraint : constraints.constraints()) {
        std::visit(
            Overloaded {
                [&](const RequiresEmptyFailure& requirement) noexcept {
                    if (!members(requirement.term).empty()) {
                        failure = diagnostics.error(empty_requirement_diagnostic(
                            requirement,
                            provenance,
                            members(requirement.term),
                            type_names
                        ));
                    }
                },
                [&](const RequiresNonEmptyFailure& requirement) noexcept {
                    if (members(requirement.term).empty()) {
                        failure = diagnostics.error(
                            DiagnosticBuilder(
                                DiagnosticCode::EffectPropagateRedundant,
                                "'?' requires a fallible expression"
                            )
                                .primary(provenance.source_span(requirement.origin))
                                .build()
                        );
                    }
                },
                [&](const RequiresFailureSubset& requirement) noexcept {
                    if (!subset(members(requirement.actual), members(requirement.allowed))) {
                        failure = diagnostics.error(subset_diagnostic(requirement, provenance));
                    }
                },
                [&](const RequiresEqualFailures& requirement) noexcept {
                    if (!std::ranges::equal(
                            members(requirement.left),
                            members(requirement.right)
                        )) {
                        failure = diagnostics.error(
                            DiagnosticBuilder(
                                DiagnosticCode::TypeMismatch,
                                "writable callable storage requires identical failure contracts"
                            )
                                .primary(provenance.source_span(requirement.origin))
                                .build()
                        );
                    }
                },
                [&](const RequiresDeclaredFailureContract& requirement) noexcept {
                    if (!members(requirement.actual).empty()) {
                        failure = diagnostics.error(
                            DiagnosticBuilder(
                                DiagnosticCode::EffectThrowPublished,
                                "entry or published function with failures requires an explicit 'throw' clause"
                            )
                                .primary(provenance.source_span(requirement.origin))
                                .build()
                        );
                    }
                },
            },
            constraint
        );
    }
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    return FailureSolution(constraints.owner(), std::move(failure_ids));
}
