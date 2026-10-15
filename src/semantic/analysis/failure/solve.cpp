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

auto sorted_names(
    std::span<const TypeID> members,
    const FailureTypeDiagnosticNames& type_names
) noexcept -> std::vector<std::string> {
    auto names = std::vector<std::string>();
    names.reserve(members.size());
    for (const auto member : members) {
        names.push_back(type_names.name(member));
    }
    std::ranges::sort(names);
    return names;
}

auto joined_names(std::span<const std::string> names, std::string_view separator) noexcept
    -> std::string {
    auto text = std::string();
    for (const auto& name : names) {
        if (!text.empty()) {
            text += separator;
        }
        text += name;
    }
    return text;
}

// A 'throw' clause names types as the declaring source spells them.
auto throw_clause(std::span<const std::string> names) noexcept -> std::string {
    auto text = std::string();
    for (const auto& name : names) {
        if (!text.empty()) {
            text += " + ";
        }
        text += std::string_view(name).substr(name.rfind('.') + 1uz);
    }
    return text;
}

// A requirement names the boundary that rejects a failure; the propagation
// sites name the expressions that deliver it there.
struct FailureEvidence final {
    std::vector<TypeID> members;
    std::vector<ProgramOriginID> sites;
};

constexpr auto maximum_reported_sites = 3uz;

auto attach_sites(
    DiagnosticBuilder& diagnostic,
    const FailureEvidence& evidence,
    CompilationProvenanceReader provenance,
    ProgramOriginID boundary,
    std::string_view primary_label
) noexcept -> void {
    if (evidence.sites.empty()) {
        diagnostic.primary(provenance.source_span(boundary));
        return;
    }
    diagnostic.primary(provenance.source_span(evidence.sites.front()), std::string(primary_label));
    for (const auto site :
         evidence.sites | std::views::drop(1) | std::views::take(maximum_reported_sites - 1uz)) {
        diagnostic.related(provenance.source_span(site), "also leaves from here");
    }
}

auto empty_requirement_diagnostic(
    const RequiresEmptyFailure& requirement,
    CompilationProvenanceReader provenance,
    const FailureEvidence& evidence,
    const FailureTypeDiagnosticNames& type_names
) noexcept -> Diagnostic {
    const auto names = sorted_names(evidence.members, type_names);
    switch (requirement.kind) {
        case EmptyFailureRequirementKind::OrdinaryConsumption:
            return DiagnosticBuilder(
                       DiagnosticCode::EffectUnmarked,
                       "failure-producing expression requires explicit '?' propagation"
            )
                .primary(provenance.source_span(requirement.origin))
                .note(
                    names.empty()
                        ? "the expression has a dependent failure contract"
                        : std::format("the expression can fail with: {}", joined_names(names, ", "))
                )
                .help("append '?' to propagate the failure, or handle it with 'try' and 'catch'")
                .build();
        case EmptyFailureRequirementKind::CatchResidual: {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::EffectCatchNonExhaustive,
                "catch does not cover every protected failure"
            );
            diagnostic.primary(provenance.source_span(requirement.origin));
            if (names.empty()) {
                diagnostic.note("the protected failure contract depends on generic parameters");
            }
            for (const auto& name : names) {
                diagnostic.note(std::format("failure type not fully covered: {}", name));
            }
            return diagnostic.build();
        }
        case EmptyFailureRequirementKind::RootBoundary: {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::EffectRootUnhandled,
                names.empty()
                    ? "test has an unproved dependent failure boundary"
                    : std::format("test leaves failures unhandled: {}", joined_names(names, ", "))
            );
            attach_sites(
                diagnostic,
                evidence,
                provenance,
                requirement.origin,
                "failure leaves the test here"
            );
            diagnostic.help(
                "a test must handle every failure; wrap the expression in 'try' and 'catch'"
            );
            return diagnostic.build();
        }
    }
    std::unreachable();
}

auto subset_diagnostic(
    const RequiresFailureSubset& requirement,
    CompilationProvenanceReader provenance,
    const FailureEvidence& evidence,
    const FailureTypeDiagnosticNames& type_names
) noexcept -> Diagnostic {
    const auto names = sorted_names(evidence.members, type_names);
    switch (requirement.kind) {
        case FailureSubsetRequirementKind::DeclaredCallable: {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::EffectSignatureBound,
                names.empty()
                    ? "callable body does not satisfy its declared failure contract for every generic argument"
                    : std::format(
                          "callable body exceeds its declared failure contract: {}",
                          joined_names(names, ", ")
                      )
            );
            attach_sites(
                diagnostic,
                evidence,
                provenance,
                requirement.origin,
                "undeclared failure leaves the callable here"
            );
            diagnostic.help(
                names.empty()
                    ? "declare the dependent failures in the 'throw' clause, or handle them"
                    : std::format(
                          "add '{}' to the 'throw' clause, or handle it with 'try' and 'catch'",
                          throw_clause(names)
                      )
            );
            return diagnostic.build();
        }
        case FailureSubsetRequirementKind::CallableAdoption:
            return DiagnosticBuilder(
                       DiagnosticCode::TypeMismatch,
                       "callable source failures exceed the target callable view"
            )
                .primary(provenance.source_span(requirement.origin))
                .note(
                    names.empty()
                        ? "the source contract is not proved to be a subset for every generic argument"
                        : std::format(
                              "failure types absent from the view: {}",
                              joined_names(names, ", ")
                          )
                )
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
    const auto qualify = [&](ModuleID module, ProgramSpellingID name) noexcept {
        const auto module_decl = declarations.module_decl(module);
        return std::format(
            "{}.{}",
            provenance.module_path_copy(module_decl.provenance_module).value(),
            provenance.spelling_copy(name)
        );
    };
    const auto canonical = types.copy(type);
    if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
        const auto declaration = declarations.structure(structure->structure);
        return qualify(declaration.module_id, declaration.name);
    }
    if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value)) {
        const auto declaration = declarations.enumeration(enumeration->enumeration);

        return qualify(declaration.module_id, declaration.name);
    }
    invariant_violation("failure diagnostic requires a nominal type");
}

FailureSolution::FailureSolution(
    ProgramIdentity identity,
    std::vector<ConstructionFailureRef> solutions,
    std::shared_ptr<GenericFailureAlgebra> generic_failures,
    ImmutableProgramTable<FailureTerm, FailureTermID> terms,
    std::map<GenericFailureID, FailureSetID> source_closed_generic_sets,
    std::vector<RequiresNonEmptyFailure> conditional_non_empty,
    std::vector<GenericFailureID> upper_bounds,
    std::vector<std::vector<TypeID>> known_members,
    FailureSetStore failure_sets,
    std::vector<bool> pending_terms
) noexcept
    : program_identity(identity),
      failures_by_term(std::move(solutions)),
      symbolic_graph(std::move(generic_failures)),
      term_graph(std::move(terms)),
      closed_generic_sets(std::move(source_closed_generic_sets)),
      conditional_propagations(std::move(conditional_non_empty)),
      upper_bounds_by_term(std::move(upper_bounds)),
      known_members_by_term(std::move(known_members)),
      known_sets(std::move(failure_sets)),
      pending_by_term(std::move(pending_terms)) {}

auto FailureSolution::owner() const noexcept -> ProgramIdentity {
    return program_identity;
}

auto FailureSolution::contains(FailureTermID term) const noexcept -> bool {
    return term.owner() == program_identity
        && static_cast<std::size_t>(term.index()) < failures_by_term.size();
}

auto FailureSolution::failure_set(ConstructionFailureRef source) const noexcept -> FailureSetID {
    if (const auto closed = closed_set(source)) {
        return *closed;
    }
    invariant_violation("a dependent failure expression was demanded as a closed set");
}

auto FailureSolution::normalize(ConstructionFailureRef source) const noexcept
    -> ConstructionFailureRef {
    if (const auto* known = std::get_if<FailureSetID>(&source)) {
        if (known->owner() != program_identity) {
            invariant_violation("failure solution used a foreign known set");
        }
        return *known;
    }
    if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
        if (!symbolic_graph->contains(*symbolic)) {
            invariant_violation("failure solution used a foreign or invalid symbolic node");
        }
        if (const auto found = closed_generic_sets.find(*symbolic);
            found != closed_generic_sets.end()) {
            return found->second;
        }
        return *symbolic;
    }
    const auto term = std::get<FailureTermID>(source);
    if (!contains(term)) {
        invariant_violation("failure solution lookup used a foreign or invalid term identity");
    }
    return pending_by_term[term.index()] ? ConstructionFailureRef(term)
                                         : failures_by_term[term.index()];
}

auto FailureSolution::closed_set(ConstructionFailureRef source) const noexcept
    -> std::optional<FailureSetID> {
    if (const auto* term = std::get_if<FailureTermID>(&source); term && !contains(*term)) {
        if (term->owner() != program_identity) {
            invariant_violation("failure solution used a foreign pending term");
        }
        return std::nullopt;
    }
    const auto normalized = normalize(source);
    if (const auto* known = std::get_if<FailureSetID>(&normalized)) {
        return *known;
    }
    return std::nullopt;
}

auto FailureSolution::generic_failures() const noexcept -> const GenericFailureAlgebra& {
    return *symbolic_graph;
}

auto FailureSolution::term_copy(FailureTermID term) const noexcept -> FailureTerm {
    return term_graph.get(term);
}

auto FailureSolution::conditional_non_empty() const noexcept
    -> std::span<const RequiresNonEmptyFailure> {
    return conditional_propagations;
}

auto FailureSolution::support(ConstructionFailureRef source) const noexcept -> GenericFailureMask {
    source = normalize(source);
    if (const auto* known = std::get_if<FailureSetID>(&source)) {
        return {.complemented = false, .members = known_sets.failure_set(*known).members};
    }
    if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
        return symbolic_graph->support(*symbolic);
    }
    const auto term = std::get<FailureTermID>(source);
    if (pending_by_term[term.index()]) {
        return {.complemented = true, .members = {}};
    }
    return symbolic_graph->support(upper_bounds_by_term[term.index()]);
}

auto FailureSolution::may_contain(ConstructionFailureRef source, TypeID member) const noexcept
    -> bool {
    if (member.owner() != owner()) {
        invariant_violation("failure membership query used a foreign nominal type");
    }
    const auto mask = support(source);
    return std::ranges::binary_search(mask.members, member) != mask.complemented;
}

auto FailureSolution::known_members(ConstructionFailureRef source) const noexcept
    -> std::vector<TypeID> {
    source = normalize(source);
    if (const auto* known = std::get_if<FailureSetID>(&source)) {
        return known_sets.failure_set(*known).members;
    }
    if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
        return symbolic_graph->known_members(*symbolic);
    }
    return known_members_by_term[std::get<FailureTermID>(source).index()];
}

auto solve_failure_constraints(
    FrozenFailureConstraints&& constraints,
    FailureSetStoreBuilder& failure_sets,
    CompilationProvenanceReader provenance,
    const FailureTypeDiagnosticNames& type_names,
    AnalysisDiagnostics diagnostics,
    bool validate_requirements
) noexcept -> AnalysisResult<FailureSolution> {
    if (constraints.owner() != failure_sets.owner()) {
        invariant_violation("failure solver inputs belong to different semantic programs");
    }
    if (constraints.provenance_owner() != provenance.identity()) {
        invariant_violation("failure solver received a foreign provenance view");
    }

    const auto& terms = constraints.terms();
    auto& algebra = *constraints.symbolic_graph;
    auto has_symbolic_sources = false;
    auto values = std::vector<std::vector<TypeID>>(terms.size());
    auto dependents = std::vector<std::vector<std::uint32_t>>(terms.size());
    auto term_ids = std::vector<FailureTermID>();
    term_ids.reserve(terms.size());
    const auto members = [&](ConstructionFailureRef source) noexcept -> std::vector<TypeID> {
        if (const auto* known = std::get_if<FailureSetID>(&source)) {
            return failure_sets.copy(*known).members;
        }
        if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
            return algebra.copy(algebra.lower_bound(*symbolic)).concrete_members;
        }
        const auto term = std::get<FailureTermID>(source);
        if (!terms.contains(term)) {
            invariant_violation("failure source refers to a foreign or invalid term");
        }
        return values[term.index()];
    };
    for (const auto [term_id, term] : terms.entries()) {
        term_ids.push_back(term_id);
        values[term_id.index()] = term.direct_members;
        const auto depend = [&](ConstructionFailureRef source) noexcept {
            if (const auto* input = std::get_if<FailureTermID>(&source)) {
                if (!terms.contains(*input)) {
                    invariant_violation("failure term depends on a foreign or invalid term");
                }
                dependents[input->index()].push_back(term_id.index());
            } else if (const auto* known = std::get_if<FailureSetID>(&source)) {
                static_cast<void>(failure_sets.copy(*known));
            } else {
                static_cast<void>(algebra.copy(std::get<GenericFailureID>(source)));
                has_symbolic_sources = true;
            }
        };
        for (const auto input : term.inputs) {
            depend(input);
        }
        for (const auto& guarded : term.guarded_inputs) {
            depend(guarded.gate);
            depend(guarded.source);
        }
        remove_members(values[term_id.index()], term.excluded_members);
        if (term.retained_members.has_value()) {
            retain_members(values[term_id.index()], *term.retained_members);
        }
    }
    for (auto& targets : dependents) {
        std::ranges::sort(targets);
        targets.erase(std::ranges::unique(targets).begin(), targets.end());
    }

    auto worklist = std::deque<std::uint32_t>();
    auto queued = std::vector<std::uint8_t>(terms.size(), 0u);
    // Known set inputs also contribute without a mutable predecessor. Enter
    // every transfer once, then revisit only consumers of growing terms.
    for (auto index = 0uz; index < terms.size(); ++index) {
        queued[index] = 1u;
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
            static_cast<void>(merge_members(next, members(input)));
        }
        for (const auto& guarded : term.guarded_inputs) {
            if (!members(guarded.gate).empty()) {
                static_cast<void>(merge_members(next, members(guarded.source)));
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

    // Concrete programs use the original exact least-set worklist above. For a
    // dependent graph, the same transfer graph also computes finite normal-row
    // support and proof bounds; these bounds are never published as its effect.
    enum class BoundMode { Support, Lower, Upper };
    auto support_rows = std::vector<GenericFailureID>();
    auto lower_rows = std::vector<GenericFailureID>();
    auto upper_rows = std::vector<GenericFailureID>();
    const auto concrete_node = [&](std::vector<TypeID> members) noexcept {
        return algebra.intern(
            {.concrete_members = std::move(members), .parameters = {}, .guards = {}}
        );
    };
    const auto empty_node = concrete_node({});
    const auto solve_rows = [&](BoundMode mode) noexcept -> std::vector<GenericFailureID> {
        auto rows = std::vector<GenericFailureID>(terms.size(), empty_node);
        const auto row_of = [&](ConstructionFailureRef source) noexcept -> GenericFailureID {
            if (const auto* term = std::get_if<FailureTermID>(&source)) {
                return rows[term->index()];
            }
            if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
                return mode == BoundMode::Lower ? algebra.lower_bound(*symbolic)
                                                : algebra.upper_bound(*symbolic);
            }
            return concrete_node(failure_sets.copy(std::get<FailureSetID>(source)).members);
        };
        const auto support_of = [&](ConstructionFailureRef source) noexcept -> GenericFailureID {
            if (const auto* term = std::get_if<FailureTermID>(&source)) {
                return support_rows[term->index()];
            }
            if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
                return algebra.upper_bound(*symbolic);
            }
            return concrete_node(failure_sets.copy(std::get<FailureSetID>(source)).members);
        };
        auto pending = std::deque<std::uint32_t>();
        auto in_queue = std::vector<std::uint8_t>(terms.size(), 1u);
        for (auto index = 0uz; index < terms.size(); ++index) {
            pending.push_back(static_cast<std::uint32_t>(index));
        }
        while (!pending.empty()) {
            const auto index = pending.front();
            pending.pop_front();
            in_queue[index] = 0u;
            const auto& term = terms.get(term_ids[index]);
            auto sources = std::vector<GenericFailureID> {concrete_node(term.direct_members)};
            for (const auto input : term.inputs) {
                sources.push_back(row_of(input));
            }
            for (const auto& contribution : term.guarded_inputs) {
                const auto gate = mode == BoundMode::Upper ? support_of(contribution.gate)
                                                           : row_of(contribution.gate);
                const auto enabled = mode == BoundMode::Lower
                    ? algebra.non_empty(gate) == FailureProof::Proven
                    : algebra.empty(gate) != FailureProof::Proven;
                if (enabled) {
                    sources.push_back(row_of(contribution.source));
                }
            }
            auto next = algebra.unite(sources);
            if (!term.excluded_members.empty()) {
                next = algebra.residual(next, term.excluded_members);
            }
            if (term.retained_members) {
                next = algebra.intersection(next, *term.retained_members);
            }
            if (next == rows[index]) {
                continue;
            }
            if (algebra.subset(rows[index], next) != FailureProof::Proven) {
                invariant_violation("symbolic failure bound transfer was not monotone");
            }
            rows[index] = next;
            for (const auto dependent : dependents[index]) {
                if (in_queue[dependent] == 0u) {
                    in_queue[dependent] = 1u;
                    pending.push_back(dependent);
                }
            }
        }
        return rows;
    };
    auto normalized = std::vector<ConstructionFailureRef>();
    normalized.reserve(failure_ids.size());
    for (const auto id : failure_ids) {
        normalized.emplace_back(id);
    }
    if (has_symbolic_sources) {
        support_rows = solve_rows(BoundMode::Support);
        lower_rows = solve_rows(BoundMode::Lower);
        upper_rows = solve_rows(BoundMode::Upper);
        auto exact_rows = std::vector<std::optional<GenericFailureID>>(terms.size());
        auto visiting = std::vector<std::uint8_t>(terms.size(), 0u);
        const auto expand =
            [&](auto&& self,
                ConstructionFailureRef source) noexcept -> std::optional<GenericFailureID> {
            if (const auto* known = std::get_if<FailureSetID>(&source)) {
                return concrete_node(failure_sets.copy(*known).members);
            }
            if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
                return *symbolic;
            }
            const auto id = std::get<FailureTermID>(source);
            if (lower_rows[id.index()] == upper_rows[id.index()]) {
                return lower_rows[id.index()];
            }
            if (visiting[id.index()] != 0u) {
                return exact_rows[id.index()];
            }
            visiting[id.index()] = 1u;
            const auto& term = terms.get(id);
            auto sources = std::vector<GenericFailureID> {concrete_node(term.direct_members)};
            for (const auto input : term.inputs) {
                const auto value = self(self, input);
                if (!value) {
                    return std::nullopt;
                }
                sources.push_back(*value);
            }
            for (const auto& contribution : term.guarded_inputs) {
                const auto gate = self(self, contribution.gate);
                const auto value = self(self, contribution.source);
                if (!gate || !value) {
                    return std::nullopt;
                }
                sources.push_back(algebra.guard(*gate, *value));
            }
            auto result = algebra.unite(sources);
            result = algebra.residual(result, term.excluded_members);
            if (term.retained_members) {
                result = algebra.intersection(result, *term.retained_members);
            }
            exact_rows[id.index()] = result;
            visiting[id.index()] = 2u;
            return result;
        };
        for (const auto id : term_ids) {
            if (const auto exact = expand(expand, id)) {
                const auto expression = algebra.copy(*exact);
                if (expression.parameters.empty() && expression.guards.empty()) {
                    normalized[id.index()] = failure_sets.intern(expression.concrete_members);
                } else {
                    normalized[id.index()] = *exact;
                }
            } else {
                // Keep the exact recursive equations and their identity. The
                // upper bound only proves requirements; it is not a solution.
                normalized[id.index()] = id;
            }
        }
    }
    const auto proof_bounds = [&](ConstructionFailureRef source,
                                  bool upper) noexcept -> GenericFailureID {
        if (const auto* term = std::get_if<FailureTermID>(&source)) {
            const auto result = normalized[term->index()];
            if (std::holds_alternative<FailureTermID>(result)) {
                return upper ? upper_rows[term->index()] : lower_rows[term->index()];
            }
            if (const auto* known = std::get_if<FailureSetID>(&result)) {
                return concrete_node(failure_sets.copy(*known).members);
            }
            const auto symbolic = std::get<GenericFailureID>(result);
            return upper ? algebra.upper_bound(symbolic) : algebra.lower_bound(symbolic);
        }
        if (const auto* known = std::get_if<FailureSetID>(&source)) {
            return concrete_node(failure_sets.copy(*known).members);
        }
        const auto symbolic = std::get<GenericFailureID>(source);
        return upper ? algebra.upper_bound(symbolic) : algebra.lower_bound(symbolic);
    };
    const auto exact_source =
        [&](ConstructionFailureRef source) noexcept -> std::optional<GenericFailureID> {
        if (const auto* term = std::get_if<FailureTermID>(&source)) {
            source = normalized[term->index()];
        }
        if (const auto* known = std::get_if<FailureSetID>(&source)) {
            return concrete_node(failure_sets.copy(*known).members);
        }
        if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
            return *symbolic;
        }
        return std::nullopt;
    };
    const auto proves_subset = [&](ConstructionFailureRef actual,
                                   ConstructionFailureRef allowed) noexcept {
        const auto left = exact_source(actual);
        const auto right = exact_source(allowed);
        if (left && right) {
            return algebra.subset(*left, *right);
        }
        if (actual == allowed) {
            return FailureProof::Proven;
        }
        if (algebra.subset(proof_bounds(actual, true), proof_bounds(allowed, false))
            == FailureProof::Proven) {
            return FailureProof::Proven;
        }
        if (algebra.subset(proof_bounds(actual, false), proof_bounds(allowed, true))
            == FailureProof::Disproven) {
            return FailureProof::Disproven;
        }
        return FailureProof::Unknown;
    };
    const auto proves_empty = [&](ConstructionFailureRef source) noexcept {
        if (const auto exact = exact_source(source)) {
            return algebra.empty(*exact);
        }
        if (algebra.empty(proof_bounds(source, true)) == FailureProof::Proven) {
            return FailureProof::Proven;
        }
        if (algebra.non_empty(proof_bounds(source, false)) == FailureProof::Proven) {
            return FailureProof::Disproven;
        }
        return FailureProof::Unknown;
    };
    auto conditional_propagations = std::vector<RequiresNonEmptyFailure>();

    auto failure = std::optional<AnalysisFailure>();
    // Walks contributions back from a rejecting source to the throw statements and
    // nearest '?' operands that deliver one of the rejected members; deeper
    // operands belong to callees.
    auto propagation_origins = std::map<ConstructionFailureRef, ProgramOriginID>();
    for (const auto& constraint : constraints.constraints()) {
        if (const auto* propagation = std::get_if<RequiresNonEmptyFailure>(&constraint)) {
            propagation_origins.try_emplace(propagation->source, propagation->origin);
        }
    }
    const auto evidence_for = [&](ConstructionFailureRef rejecting,
                                  std::vector<TypeID> rejected) noexcept -> FailureEvidence {
        auto sites = std::vector<ProgramOriginID>();
        auto visited = std::set<ConstructionFailureRef> {rejecting};
        auto pending = std::vector<ConstructionFailureRef> {rejecting};
        while (!pending.empty()) {
            const auto source = pending.back();
            pending.pop_back();
            auto delivered = members(source);
            retain_members(delivered, rejected);
            if (delivered.empty()) {
                continue;
            }
            if (const auto origin = propagation_origins.find(source);
                origin != propagation_origins.end()) {
                sites.push_back(origin->second);
                continue;
            }
            const auto* term_id = std::get_if<FailureTermID>(&source);
            if (!term_id) {
                continue;
            }
            const auto& term = terms.get(*term_id);
            for (const auto& thrown : term.throw_sites) {
                if (std::ranges::contains(rejected, thrown.member)
                    && !std::ranges::contains(sites, thrown.origin)) {
                    sites.push_back(thrown.origin);
                }
            }
            const auto visit = [&](ConstructionFailureRef input) noexcept {
                if (visited.insert(input).second) {
                    pending.push_back(input);
                }
            };
            std::ranges::for_each(term.inputs, visit);
            for (const auto& guarded : term.guarded_inputs) {
                visit(guarded.source);
            }
        }
        std::ranges::sort(sites, {}, [&](ProgramOriginID site) noexcept {
            const auto span = provenance.source_span(site);
            return std::pair(span.source_id.index(), span.span.start());
        });
        return {.members = std::move(rejected), .sites = std::move(sites)};
    };
    constexpr auto copy = [](std::span<const TypeID> source) static noexcept {
        return std::vector<TypeID>(source.begin(), source.end());
    };
    for (const auto& constraint : constraints.constraints()) {
        if (!validate_requirements) {
            continue;
        }
        constraint.visit(
            Overloaded {
                [&](const RequiresEmptyFailure& requirement) noexcept {
                    if (proves_empty(requirement.source) != FailureProof::Proven) {
                        failure = diagnostics.error(empty_requirement_diagnostic(
                            requirement,
                            provenance,
                            evidence_for(requirement.source, copy(members(requirement.source))),
                            type_names
                        ));
                    }
                },
                [&](const RequiresNonEmptyFailure& requirement) noexcept {
                    const auto proof = proves_empty(requirement.source);
                    if (proof == FailureProof::Unknown && requirement.allow_dependent) {
                        conditional_propagations.push_back(requirement);
                    } else if (proof != FailureProof::Disproven) {
                        failure = diagnostics.error(
                            DiagnosticBuilder(
                                DiagnosticCode::EffectPropagateRedundant,
                                "'?' requires a fallible expression"
                            )
                                .primary(provenance.source_span(requirement.origin))
                                .help("remove '?'; the operand cannot fail")
                                .build()
                        );
                    }
                },
                [&](const RequiresFailureSubset& requirement) noexcept {
                    if (proves_subset(requirement.actual, requirement.allowed)
                        != FailureProof::Proven) {
                        auto excess = copy(members(requirement.actual));
                        remove_members(excess, members(requirement.allowed));
                        failure = diagnostics.error(subset_diagnostic(
                            requirement,
                            provenance,
                            evidence_for(requirement.actual, std::move(excess)),
                            type_names
                        ));
                    }
                },
                [&](const RequiresEqualFailures& requirement) noexcept {
                    if (proves_subset(requirement.left, requirement.right) != FailureProof::Proven
                        || proves_subset(requirement.right, requirement.left)
                            != FailureProof::Proven) {
                        failure = diagnostics.error(
                            DiagnosticBuilder(
                                DiagnosticCode::TypeMismatch,
                                "failure contracts must be identical"
                            )
                                .primary(provenance.source_span(requirement.origin))
                                .build()
                        );
                    }
                },
                [&](const RequiresDeclaredFailureContract& requirement) noexcept {
                    if (proves_empty(requirement.actual) != FailureProof::Proven) {
                        const auto names = sorted_names(members(requirement.actual), type_names);
                        failure = diagnostics.error(
                            DiagnosticBuilder(
                                DiagnosticCode::EffectThrowPublished,
                                "explicit entry or published function with failures requires a 'throw' clause"
                            )
                                .primary(provenance.source_span(requirement.origin))
                                .help(
                                    std::format(
                                        "declare 'throw {}' in the signature, or handle the "
                                        "failures in the body",
                                        throw_clause(names)
                                    )
                                )
                                .build()
                        );
                    }
                },
            }
        );
    }
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    auto pending_terms = std::vector<bool>(terms.size(), false);
    auto pending_work = std::vector<std::uint32_t>();
    for (const auto application : constraints.pending_applications) {
        pending_terms[application.index()] = true;
        pending_work.push_back(application.index());
    }
    for (auto index = 0uz; index < pending_work.size(); ++index) {
        for (const auto dependent : dependents[pending_work[index]]) {
            if (!pending_terms[dependent]) {
                pending_terms[dependent] = true;
                pending_work.push_back(dependent);
            }
        }
    }
    auto closed_generic_sets = std::map<GenericFailureID, FailureSetID>();
    for (const auto id : algebra.ids()) {
        const auto expression = algebra.copy(id);
        if (expression.parameters.empty() && expression.guards.empty()) {
            closed_generic_sets.emplace(id, failure_sets.intern(expression.concrete_members));
        }
    }
    return FailureSolution(
        constraints.owner(),
        std::move(normalized),
        std::move(constraints.symbolic_graph),
        std::move(constraints.term_table),
        std::move(closed_generic_sets),
        std::move(conditional_propagations),
        std::move(upper_rows),
        std::move(values),
        failure_sets.snapshot(),
        std::move(pending_terms)
    );
}
