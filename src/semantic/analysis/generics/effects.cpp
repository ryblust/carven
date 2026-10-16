module carven:semantic.analysis.generics.effects.impl;

import :diagnostics.builder;
import :semantic.analysis.program;
import :support.invariant;
import std;

auto ProgramDraft::add_generic_failure_parameter(GenericFailureParameter parameter) noexcept
    -> GenericFailureID {
    return storage.failure_constraints.add_generic_parameter(parameter);
}

auto ProgramDraft::union_generic_failures(std::vector<ConstructionFailureRef> inputs) noexcept
    -> ConstructionFailureRef {
    if (std::ranges::any_of(inputs, [](const auto& input) static noexcept {
            return std::holds_alternative<FailureTermID>(input);
        })) {
        return add_union_failure_term(std::move(inputs));
    }
    auto symbolic = std::vector<GenericFailureID>();
    for (const auto input : inputs) {
        if (const auto* generic = std::get_if<GenericFailureID>(&input)) {
            symbolic.push_back(*generic);
        } else {
            const auto set = std::get<FailureSetID>(input);
            symbolic.push_back(
                storage.failure_constraints.add_generic_concrete(failure_set_copy(set).members)
            );
        }
    }
    if (symbolic.empty()) {
        return empty_failure_set();
    }
    return storage.failure_constraints.add_generic_union(std::move(symbolic));
}

auto ProgramDraft::make_failure_instantiation(
    GenericDeclarationID definition,
    std::span<const GenericArgument> arguments
) noexcept -> FailureInstantiation {
    auto deferred = FailureParameterEnvironment();
    for (auto index = 0uz; index < arguments.size(); ++index) {
        if (const auto* failure = std::get_if<ConstructionFailureRef>(&arguments[index])) {
            deferred.emplace(
                GenericFailureParameter {definition, static_cast<std::uint32_t>(index)},
                *failure
            );
        }
    }
    return FailureInstantiation(
        storage.failure_constraints,
        storage.failure_sets,
        {
            .parameter = [definition,
                          environment = GenericEnvironment(arguments.begin(), arguments.end()),
                          constraints = &storage.failure_constraints](
                             GenericFailureParameter parameter
                         ) noexcept -> ConstructionFailureRef {
                if (parameter.definition != definition) {
                    return constraints->add_generic_parameter(parameter);
                }
                if (parameter.index >= environment.size()) {
                    invariant_violation("failure substitution used an absent generic argument");
                }
                const auto* failure =
                    std::get_if<ConstructionFailureRef>(&environment[parameter.index]);
                if (!failure) {
                    invariant_violation("failure parameter substitution selected a type argument");
                }
                return *failure;
            },
            .member = [](TypeID member) static noexcept { return member; },
            .origin = [](ProgramOriginID origin) static noexcept { return origin; },
        },
        std::move(deferred)
    );
}

auto ProgramDraft::failure_dependencies(ConstructionFailureRef source) const noexcept
    -> std::vector<ConstructionFailureRef> {
    return storage.failure_constraints.dependencies(source);
}

auto ProgramDraft::complete_failure_applications(
    std::span<const ConstructionFailureRef> roots
) noexcept -> AnalysisResult<void> {
    if (storage.failure_constraints.complete_applications(storage.failure_sets, roots)) {
        return {};
    }
    const auto parameter = *storage.failure_constraints.growing_parameter();
    const auto contract = generic_declaration_contract_copy(parameter.definition);
    return std::unexpected(analysis_diagnostics.error(
        DiagnosticBuilder(
            DiagnosticCode::TypeGenericExpansion,
            "recursive failure substitution grows a conditional effect expression"
        )
            .primary(source_span(contract.origin))
            .build()
    ));
}

auto ProgramDraft::preview_failures() noexcept -> AnalysisResult<FailureSolution> {
    return solve_failure_constraints(
        storage.failure_constraints.snapshot(),
        storage.failure_sets,
        provenance_appender.reader(),
        FailureTypeDiagnosticNames(
            storage.types,
            storage.declarations.construction_view(),
            provenance_appender.reader()
        ),
        analysis_diagnostics,
        false
    );
}

auto ProgramDraft::construction_failure_known_members(ConstructionFailureRef source) noexcept
    -> std::vector<TypeID> {
    if (const auto* closed = std::get_if<FailureSetID>(&source)) {
        return failure_set_copy(*closed).members;
    }
    const auto preview = preview_failures();
    if (!preview) {
        invariant_violation("failure preview unexpectedly rejected an unchecked requirement");
    }
    return preview->known_members(source);
}
