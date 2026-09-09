module carven:backend.generation.plan.references.impl;

import :backend.generation.plan.references;
import :support.invariant;
import :support.visit;
import std;

namespace {

struct DeclarationReferenceFacts final {
    std::flat_map<NominalDeclarationRef, TargetTypeCompleteness> requirements;
    std::flat_set<CallableID> closures;
};

class DeclarationReferenceCollector final {
public:
    explicit DeclarationReferenceCollector(const SemIRProgram& semantic) noexcept
        : semantic(semantic) {}

    auto finish() && noexcept -> DeclarationReferenceFacts { return std::move(result); }

private:
    struct RecursionGuard final {
        std::flat_set<TypeID> types;
        std::flat_set<CallableSignatureID> signatures;
    };

    auto require_nominal(
        NominalDeclarationRef nominal,
        TargetTypeCompleteness completeness
    ) noexcept -> void {
        auto& requirements = result.requirements;
        const auto found = requirements.find(nominal);
        if (found == requirements.end()) {
            requirements.emplace(nominal, completeness);
        } else if (completeness == TargetTypeCompleteness::CompleteDefinition) {
            found->second = completeness;
        }
    }

    auto collect_failure_set(
        FailureSetID failure_set,
        TargetTypeCompleteness completeness,
        RecursionGuard& guard
    ) noexcept -> void {
        for (const auto member : semantic.failure_sets().failure_set(failure_set).members) {
            collect_type(member, completeness, guard);
        }
    }

    auto collect_signature(CallableSignatureID signature_id, RecursionGuard& guard) noexcept
        -> void {
        if (!guard.signatures.insert(signature_id).second) {
            return;
        }
        const auto& signature = semantic.callable_signatures().signature(signature_id);
        for (const auto& parameter : signature.parameters) {
            collect_type(
                parameter.type,
                parameter.access == AccessMode::Read ? TargetTypeCompleteness::CompleteDefinition
                                                     : TargetTypeCompleteness::Declaration,
                guard
            );
        }
        const auto result_completeness = TargetTypeCompleteness::Declaration;
        collect_type(signature.result, result_completeness, guard);
        collect_failure_set(signature.failures, result_completeness, guard);
        guard.signatures.erase(signature_id);
    }

    auto collect_callable(CallableID callable, RecursionGuard& guard) noexcept -> void {
        collect_signature(semantic.declarations().callable(callable).signature, guard);
    }

    auto collect_type(
        TypeID type_id,
        TargetTypeCompleteness completeness,
        RecursionGuard& guard
    ) noexcept -> void {
        if (!guard.types.insert(type_id).second) {
            return;
        }
        std::visit(
            Overloaded {
                [&](const PointerTypeValue& value) noexcept {
                    collect_type(value.target, TargetTypeCompleteness::Declaration, guard);
                },
                [&](const CppTypeValue& value) noexcept {
                    for (const auto argument : cpp_type_references(value)) {
                        collect_type(argument, TargetTypeCompleteness::CompleteDefinition, guard);
                    }
                },
                [](const BuiltinTypeValue&) static noexcept {},
                [&](const StructTypeValue& value) noexcept {
                    require_nominal(NominalDeclarationRef {value.structure}, completeness);
                },
                [&](const EnumTypeValue& value) noexcept {
                    require_nominal(NominalDeclarationRef {value.enumeration}, completeness);
                },
                [&](const ArrayTypeValue& value) noexcept {
                    collect_type(value.element, completeness, guard);
                },
                [&](const FunctionTypeValue& value) noexcept {
                    collect_callable(value.callable, guard);
                },
                [&](const ClosureTypeValue& value) noexcept {
                    result.closures.insert(value.callable);
                    collect_callable(value.callable, guard);
                    const auto body_id = semantic.declarations().body_for_callable(value.callable);
                    const auto& body = semantic.bodies().body(*body_id);
                    for (const auto capture : body.inputs().captures) {
                        collect_type(
                            body.binding(capture).type,
                            TargetTypeCompleteness::CompleteDefinition,
                            guard
                        );
                    }
                },
                [&](const CallableViewTypeValue& value) noexcept {
                    collect_signature(value.signature, guard);
                },
            },
            semantic.types().type(type_id).value
        );
        guard.types.erase(type_id);
    }

public:
    auto collect_declaration(DeclarationRef declaration) noexcept -> void {
        auto guard = RecursionGuard();
        std::visit(
            Overloaded {
                [&](FunctionID id) noexcept {
                    collect_callable(semantic.declarations().function(id).callable, guard);
                },
                [&](StructID id) noexcept {
                    for (const auto& field : semantic.declarations().structure(id).fields) {
                        collect_type(field.type, TargetTypeCompleteness::CompleteDefinition, guard);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& enumeration = semantic.declarations().enumeration(id);
                    if (const auto* numeric =
                            std::get_if<NumericEnumRepresentation>(&enumeration.representation)) {
                        collect_type(
                            numeric->underlying_type,
                            TargetTypeCompleteness::CompleteDefinition,
                            guard
                        );
                    }
                    for (const auto case_id : enumeration.cases) {
                        for (const auto type :
                             semantic.declarations().enum_case(case_id).payload_types) {
                            collect_type(type, TargetTypeCompleteness::CompleteDefinition, guard);
                        }
                    }
                },
                [&](ModuleConstantID id) noexcept {
                    collect_type(
                        semantic.constants()
                            .constant(semantic.declarations().module_constant(id).value)
                            .type,
                        TargetTypeCompleteness::CompleteDefinition,
                        guard
                    );
                },
            },
            declaration
        );
    }

private:
    const SemIRProgram& semantic;
    DeclarationReferenceFacts result;
};

} // namespace

auto target_visibility(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> DeclarationVisibility {
    return std::visit(
        Overloaded {
            [&](FunctionID id) noexcept { return semantic.declarations().function(id).visibility; },
            [&](StructID id) noexcept { return semantic.declarations().structure(id).visibility; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).visibility; },
            [&](ModuleConstantID id) noexcept {
                return semantic.declarations().module_constant(id).visibility;
            },
        },
        declaration
    );
}

auto target_declaration_ref(NominalDeclarationRef nominal) noexcept -> DeclarationRef {
    return std::visit(
        []<typename ID>(ID id) static noexcept -> DeclarationRef {
            static_assert(std::same_as<ID, StructID> || std::same_as<ID, EnumID>);
            return id;
        },
        nominal
    );
}

auto target_owner_module(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> ModuleID {
    return std::visit(
        Overloaded {
            [&](FunctionID id) noexcept { return semantic.declarations().function(id).module_id; },
            [&](StructID id) noexcept { return semantic.declarations().structure(id).module_id; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).module_id; },
            [&](ModuleConstantID id) noexcept {
                return semantic.declarations().module_constant(id).module_id;
            },
        },
        declaration
    );
}

auto collect_target_references(
    const SemIRProgram& semantic,
    std::span<const std::vector<DeclarationRef>> surface_declarations
) noexcept -> TargetReferenceFacts {
    auto result = TargetReferenceFacts {
        .surface_requirements =
            std::vector<std::flat_map<NominalDeclarationRef, TargetTypeCompleteness>>(
                surface_declarations.size()
            ),
        .surface_closures = std::vector<std::flat_set<CallableID>>(surface_declarations.size()),
    };
    auto visited = std::vector<bool>(surface_declarations.size());
    for (const auto entry : semantic.declarations().modules()) {
        const auto index = entry.id.index();
        if (index >= surface_declarations.size() || visited[index]) {
            invariant_violation("semantic modules are not a dense owned table");
        }
        visited[index] = true;
        auto collector = DeclarationReferenceCollector(semantic);
        for (const auto declaration : surface_declarations[index]) {
            collector.collect_declaration(declaration);
        }
        auto facts = std::move(collector).finish();
        result.surface_requirements[index] = std::move(facts.requirements);
        result.surface_closures[index] = std::move(facts.closures);
    }
    if (!std::ranges::all_of(visited, std::identity {})) {
        invariant_violation("surface references contain an unknown module row");
    }
    return result;
}

auto target_nominal_dependencies(
    const SemIRProgram& semantic,
    NominalDeclarationRef nominal
) noexcept -> std::vector<NominalDeclarationRef> {
    auto collector = DeclarationReferenceCollector(semantic);
    collector.collect_declaration(target_declaration_ref(nominal));
    const auto facts = std::move(collector).finish();
    auto result = std::vector<NominalDeclarationRef>();
    for (const auto& [dependency, completeness] : facts.requirements) {
        if (completeness == TargetTypeCompleteness::CompleteDefinition) {
            result.push_back(dependency);
        }
    }
    return result;
}
