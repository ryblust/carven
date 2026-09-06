module carven:backend.generation.plan.references.impl;

import :backend.generation.plan.references;
import :support.invariant;
import :support.visit;
import std;

namespace {

class SurfaceReferenceCollector final {
public:
    SurfaceReferenceCollector(
        const SemIRProgram& source,
        std::span<const std::vector<DeclarationRef>> surfaces
    ) noexcept
        : semantic(source),
          surface_declarations(surfaces),
          result {
              .surface_requirements =
                  std::vector<std::flat_map<NominalDeclarationRef, TargetTypeCompleteness>>(
                      surfaces.size()
                  ),
          } {}

    auto finish() && noexcept -> TargetReferenceFacts {
        auto visited_modules = std::vector<bool>(surface_declarations.size());
        for (const auto module_record : semantic.declarations().modules()) {
            if (module_record.id.index() >= surface_declarations.size()
                || visited_modules[module_record.id.index()]) {
                invariant_violation("semantic modules are not a dense owned table");
            }
            visited_modules[module_record.id.index()] = true;
            for (const auto declaration : surface_declarations[module_record.id.index()]) {
                collect_declaration(module_record.id, declaration);
            }
        }
        if (!std::ranges::all_of(visited_modules, std::identity {})) {
            invariant_violation("surface references contain an unknown module row");
        }
        return std::move(result);
    }

private:
    struct RecursionGuard final {
        std::flat_set<TypeID> types;
        std::flat_set<CallableSignatureID> signatures;
    };

    auto require_nominal(
        ModuleID module_id,
        NominalDeclarationRef nominal,
        TargetTypeCompleteness completeness
    ) noexcept -> void {
        auto& requirements = result.surface_requirements[module_id.index()];
        const auto found = requirements.find(nominal);
        if (found == requirements.end()) {
            requirements.emplace(nominal, completeness);
        } else if (completeness == TargetTypeCompleteness::CompleteDefinition) {
            found->second = completeness;
        }
    }

    auto collect_failure_set(
        ModuleID module_id,
        FailureSetID failure_set,
        TargetTypeCompleteness completeness,
        RecursionGuard& guard
    ) noexcept -> void {
        for (const auto member : semantic.failure_sets().failure_set(failure_set).members) {
            collect_type(module_id, member, completeness, guard);
        }
    }

    auto collect_signature(
        ModuleID module_id,
        CallableSignatureID signature_id,
        RecursionGuard& guard
    ) noexcept -> void {
        if (!guard.signatures.insert(signature_id).second) {
            return;
        }
        const auto& signature = semantic.callable_signatures().signature(signature_id);
        for (const auto& parameter : signature.parameters) {
            collect_type(
                module_id,
                parameter.type,
                parameter.access == AccessMode::Read ? TargetTypeCompleteness::CompleteDefinition
                                                     : TargetTypeCompleteness::Declaration,
                guard
            );
        }
        const auto result_completeness = TargetTypeCompleteness::Declaration;
        collect_type(module_id, signature.result, result_completeness, guard);
        collect_failure_set(module_id, signature.failures, result_completeness, guard);
        guard.signatures.erase(signature_id);
    }

    auto collect_callable(ModuleID module_id, CallableID callable, RecursionGuard& guard) noexcept
        -> void {
        collect_signature(module_id, semantic.declarations().callable(callable).signature, guard);
    }

    auto collect_type(
        ModuleID module_id,
        TypeID type_id,
        TargetTypeCompleteness completeness,
        RecursionGuard& guard
    ) noexcept -> void {
        if (!guard.types.insert(type_id).second) {
            return;
        }
        std::visit(
            Overloaded {
                [&](const CppTypeValue& value) noexcept {
                    if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                        for (const auto argument : named->arguments) {
                            collect_type(
                                module_id,
                                argument,
                                TargetTypeCompleteness::CompleteDefinition,
                                guard
                            );
                        }
                    } else {
                        for (const auto& operand : std::get<CppDeducedType>(value.form).operands) {
                            collect_type(
                                module_id,
                                operand.type,
                                TargetTypeCompleteness::CompleteDefinition,
                                guard
                            );
                        }
                    }
                },
                [](const BuiltinTypeValue&) static noexcept {},
                [&](const StructTypeValue& value) noexcept {
                    require_nominal(
                        module_id,
                        NominalDeclarationRef {value.structure},
                        completeness
                    );
                },
                [&](const EnumTypeValue& value) noexcept {
                    require_nominal(
                        module_id,
                        NominalDeclarationRef {value.enumeration},
                        completeness
                    );
                },
                [&](const ArrayTypeValue& value) noexcept {
                    collect_type(module_id, value.element, completeness, guard);
                },
                [&](const FunctionTypeValue& value) noexcept {
                    collect_callable(module_id, value.callable, guard);
                },
                [&](const ClosureTypeValue& value) noexcept {
                    collect_callable(module_id, value.callable, guard);
                },
                [&](const CallableViewTypeValue& value) noexcept {
                    collect_signature(module_id, value.signature, guard);
                },
            },
            semantic.types().type(type_id).value
        );
        guard.types.erase(type_id);
    }

    auto collect_declaration(ModuleID module_id, DeclarationRef declaration) noexcept -> void {
        auto guard = RecursionGuard();
        std::visit(
            Overloaded {
                [&](FunctionID id) noexcept {
                    collect_callable(
                        module_id,
                        semantic.declarations().function(id).callable,
                        guard
                    );
                },
                [&](StructID id) noexcept {
                    for (const auto& field : semantic.declarations().structure(id).fields) {
                        collect_type(
                            module_id,
                            field.type,
                            TargetTypeCompleteness::CompleteDefinition,
                            guard
                        );
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& enumeration = semantic.declarations().enumeration(id);
                    if (const auto* numeric =
                            std::get_if<NumericEnumRepresentation>(&enumeration.representation)) {
                        collect_type(
                            module_id,
                            numeric->underlying_type,
                            TargetTypeCompleteness::CompleteDefinition,
                            guard
                        );
                    }
                    for (const auto case_id : enumeration.cases) {
                        for (const auto type :
                             semantic.declarations().enum_case(case_id).payload_types) {
                            collect_type(
                                module_id,
                                type,
                                TargetTypeCompleteness::CompleteDefinition,
                                guard
                            );
                        }
                    }
                },
                [&](ModuleConstantID id) noexcept {
                    collect_type(
                        module_id,
                        semantic.declarations().module_constant(id).type,
                        TargetTypeCompleteness::CompleteDefinition,
                        guard
                    );
                },
            },
            declaration
        );
    }

    const SemIRProgram& semantic;
    std::span<const std::vector<DeclarationRef>> surface_declarations;
    TargetReferenceFacts result;
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
    return SurfaceReferenceCollector(semantic, surface_declarations).finish();
}
