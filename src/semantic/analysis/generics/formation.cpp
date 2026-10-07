module carven:semantic.analysis.generics.formation.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.traversal;
import :support.invariant;
import std;

auto ProgramDraft::require_type_formation(
    TypeFormationRequirement requirement,
    std::optional<GenericDeclarationID> definition
) noexcept -> AnalysisResult<void> {
    switch (type_formation_status(*this, requirement.kind, requirement.type)) {
        case TypeFormationStatus::Supported:
        case TypeFormationStatus::NativeDelegated: return {};
        case TypeFormationStatus::Rejected:
            return std::unexpected(
                diagnostics().error(DiagnosticBuilder(
                                        DiagnosticCode::TypeValueRequired,
                                        "ptr<void> has no object to dereference"
                )
                                        .primary(source_span(requirement.origin))
                                        .build())
            );
        case TypeFormationStatus::Dependent: {
            if (!definition) {
                invariant_violation("dependent type formation lacks a generic definition owner");
            }
            auto& function = std::get<GenericFunctionDefinition>(
                *storage.generic_definitions.at(definition->index())
            );
            function.type_formations.push_back(requirement);
            return {};
        }
    }
    std::unreachable();
}

auto ProgramDraft::check_generic_type_formations(
    GenericDeclarationID definition,
    std::span<const GenericArgument> arguments,
    ProgramOriginID origin
) noexcept -> AnalysisResult<TypeFormationClosure> {
    auto finite = validate_generic_definitions(definition);
    if (!finite) {
        return std::unexpected(finite.error());
    }
    using Key = std::pair<GenericDeclarationID, GenericEnvironment>;

    struct Pending final {
        Key key;
        ProgramOriginID origin;
    };

    // Each type slot contributes its object status to transitive requirements.
    using SlotFacts = std::optional<TypeFormationStatus>;
    using FormationKey = std::pair<GenericDeclarationID, std::vector<SlotFacts>>;
    auto visited = std::set<FormationKey>();
    auto pending = std::vector<Pending> {
        {{definition, GenericEnvironment(arguments.begin(), arguments.end())}, origin}
    };
    auto closure = TypeFormationClosure::Complete;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        auto facts = std::vector<SlotFacts>();
        for (const auto& argument : current.key.second) {
            if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                facts.push_back(
                    type_formation_status(*this, TypeFormationRequirementKind::NonVoidObject, *type)
                );
            } else {
                facts.emplace_back(std::nullopt);
            }
        }
        if (!visited.emplace(current.key.first, std::move(facts)).second) {
            continue;
        }
        if (visited.size() > maximum_generic_type_work) {
            return std::unexpected(
                diagnostics().error(DiagnosticBuilder(
                                        DiagnosticCode::TypeGenericLimits,
                                        "generic type formation checking exceeded its work budget"
                )
                                        .primary(source_span(origin))
                                        .build())
            );
        }
        const auto& [owner, environment] = current.key;
        const auto source = generic_declaration_copy(owner);
        const auto& function = std::get<GenericFunctionDefinition>(source);
        for (const auto& requirement : function.type_formations) {
            const auto type = substitute_generic_type(requirement.type, owner, environment);
            switch (type_formation_status(*this, requirement.kind, type)) {
                case TypeFormationStatus::Supported:
                case TypeFormationStatus::NativeDelegated:
                case TypeFormationStatus::Dependent:       break;
                case TypeFormationStatus::Rejected:        {
                    auto diagnostic = DiagnosticBuilder(
                        DiagnosticCode::TypeValueRequired,
                        "generic call supplies void where an object must be dereferenced"
                    );
                    diagnostic.primary(
                        source_span(origin),
                        "this type environment violates the requirement"
                    );
                    diagnostic.related(
                        source_span(requirement.origin),
                        "dereference requires a non-void object"
                    );
                    if (current.origin != origin && current.origin != requirement.origin) {
                        diagnostic.related(
                            source_span(current.origin),
                            "requirement forwarded by this generic call"
                        );
                    }
                    return std::unexpected(diagnostics().error(diagnostic.build()));
                }
            }
        }
        // An analyzing callee is an unresolved edge, never an empty contract.
        // The full definition batch and every concrete instance require closure.
        if (!has_generic_function_body(owner)) {
            closure = TypeFormationClosure::Pending;
            continue;
        }
        const auto calls = [&](const SemanticExpression& expression) noexcept {
            const auto* call = std::get_if<SemGenericCall>(&expression.value);
            if (!call) {
                return;
            }
            auto effects = make_failure_instantiation(owner, environment);
            auto types =
                substitute_generic_environment(call->environment, owner, environment, &effects);
            pending.push_back({{call->definition, std::move(types)}, expression.origin});
        };
        const auto& body = generic_function_body(owner);
        visit_semantic_nodes(body.region, calls);
        if (body.residual) {
            visit_semantic_nodes(*body.residual, calls);
        }
    }
    return closure;
}

auto ProgramDraft::validate_generic_type_formations() noexcept -> AnalysisResult<void> {
    // Parameter-flow checking closes recursive constructor growth before any
    // transitive requirement is expanded through the checked call graph.
    auto finite = validate_generic_definitions();
    if (!finite) {
        return finite;
    }
    for (const auto definition : generic_declaration_ids()) {
        const auto source = generic_declaration_copy(definition);
        const auto* function = std::get_if<GenericFunctionDefinition>(&source);
        if (!function) {
            continue;
        }
        auto environment = generic_parameter_environment(definition);
        auto checked =
            check_generic_type_formations(definition, environment, function->contract.origin);
        if (!checked) {
            return std::unexpected(checked.error());
        }
        if (*checked != TypeFormationClosure::Complete) {
            invariant_violation("generic formation checking began before definition closure");
        }
    }
    return {};
}
