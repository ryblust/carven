module carven:backend.generation.plan.references.impl;

import :backend.generation.plan.references;
import :semantic.semir.generic;
import :support.invariant;
import :support.visit;
import std;

namespace {

enum class ReferencePurpose { Declaration, ProviderAccess, Layout };

struct DeclarationReferenceFacts final {
    std::flat_map<TargetTypeDefinitionRef, TargetTypeCompleteness> requirements;
    std::flat_set<CallableID> callables;
    std::flat_set<NominalDeclarationRef> host_nominals;
};

class DeclarationReferenceCollector final {
public:
    // Each module publishes the references exposed by its own definitions.
    DeclarationReferenceCollector(
        const SemIRProgram& semantic,
        std::optional<ModuleID> surface_module
    ) noexcept;

    auto finish() && noexcept -> DeclarationReferenceFacts;

private:
    auto require_type(
        TargetTypeDefinitionRef definition,
        TargetTypeCompleteness completeness
    ) noexcept -> void;

    auto collect_failure_set(
        FailureSetID failure_set,
        TargetTypeCompleteness completeness,
        ReferencePurpose purpose
    ) noexcept -> void;

    auto collect_signature(
        CallableSignatureID signature_id,
        TargetTypeCompleteness value_completeness,
        ReferencePurpose purpose,
        std::optional<CallableID> callable = std::nullopt
    ) noexcept -> void;

    auto collect_callable(CallableID callable, ReferencePurpose purpose) noexcept -> void;

    auto collect_type(
        TypeID type_id,
        TargetTypeCompleteness completeness,
        ReferencePurpose purpose
    ) noexcept -> void;

    auto collect_nominal_members(NominalDeclarationRef nominal, ReferencePurpose purpose) noexcept
        -> void;

    auto collect_surface(CallableID callable) noexcept -> void;
    auto collect_body_callable(CallableID callable) noexcept -> void;

public:
    auto collect_declaration(DeclarationRef declaration) noexcept -> void;
    auto collect_generic(GenericDeclarationID definition) noexcept -> void;
    auto collect_boundary(FunctionID id) noexcept -> void;
    auto collect_definition(
        TargetTypeDefinitionRef definition,
        ReferencePurpose purpose = ReferencePurpose::Layout
    ) noexcept -> void;

private:
    const SemIRProgram& semantic;
    std::optional<ModuleID> surface_module;
    std::flat_map<CallableID, FunctionID> module_functions;
    DeclarationReferenceFacts result;
    std::set<std::tuple<TypeID, TargetTypeCompleteness, ReferencePurpose>> visited_types;
    std::set<std::tuple<
        CallableSignatureID,
        TargetTypeCompleteness,
        ReferencePurpose,
        std::optional<CallableID>>>
        visited_signatures;
    std::set<CallableID> visited_surfaces;
    std::set<GenericDeclarationID> visited_generics;
};

DeclarationReferenceCollector::DeclarationReferenceCollector(
    const SemIRProgram& semantic,
    std::optional<ModuleID> surface_module
) noexcept
    : semantic(semantic),
      surface_module(surface_module),
      result {.requirements = {}, .callables = {}, .host_nominals = {}} {
    if (!surface_module) {
        return;
    }
    for (const auto item : semantic.declarations().module_decl(*surface_module).items) {
        if (const auto* function = std::get_if<FunctionID>(&item)) {
            module_functions.emplace(
                semantic.declarations().function(*function).callable,
                *function
            );
        }
    }
}

auto DeclarationReferenceCollector::finish() && noexcept -> DeclarationReferenceFacts {
    return std::move(result);
}

auto DeclarationReferenceCollector::require_type(
    TargetTypeDefinitionRef definition,
    TargetTypeCompleteness completeness
) noexcept -> void {
    auto& requirements = result.requirements;
    const auto found = requirements.find(definition);
    if (found == requirements.end()) {
        requirements.emplace(definition, completeness);
    } else if (completeness > found->second) {
        found->second = completeness;
    }
}

auto DeclarationReferenceCollector::collect_failure_set(
    FailureSetID failure_set,
    TargetTypeCompleteness completeness,
    ReferencePurpose purpose
) noexcept -> void {
    for (const auto member : semantic.failure_sets().failure_set(failure_set).members) {
        collect_type(member, completeness, purpose);
    }
}

auto DeclarationReferenceCollector::collect_signature(
    CallableSignatureID signature_id,
    TargetTypeCompleteness value_completeness,
    ReferencePurpose purpose,
    std::optional<CallableID> callable
) noexcept -> void {
    if (!visited_signatures.emplace(signature_id, value_completeness, purpose, callable).second) {
        return;
    }
    const auto& signature = semantic.callable_signatures().signature(signature_id);
    for (const auto [index, parameter] : std::views::enumerate(signature.parameters)) {
        if (parameter.stage == ParameterStage::Static) {
            continue;
        }
        const auto contents = semantic.type_contents(parameter.type);
        const auto needs_native_layout = parameter.access == AccessMode::Read
            && contents.contains_native_value
            && !contents.contains_carven_owner()
            && !(callable && semantic.read_requires_stable_storage(*callable, index));
        collect_type(
            parameter.type,
            purpose == ReferencePurpose::Layout
                ? (needs_native_layout ? TargetTypeCompleteness::CompleteDefinition
                                       : TargetTypeCompleteness::Declaration)
                : purpose == ReferencePurpose::ProviderAccess ? value_completeness
                : parameter.access == AccessMode::Read ? TargetTypeCompleteness::CompleteDefinition
                : parameter.access == AccessMode::Take ? value_completeness
                                                       : TargetTypeCompleteness::Declaration,
            purpose
        );
    }
    collect_type(signature.result, value_completeness, purpose);
    collect_failure_set(signature.failures, value_completeness, purpose);
}

auto DeclarationReferenceCollector::collect_callable(
    CallableID callable,
    ReferencePurpose purpose
) noexcept -> void {
    collect_signature(
        semantic.declarations().callable(callable).signature,
        TargetTypeCompleteness::Declaration,
        purpose
    );
}

auto DeclarationReferenceCollector::collect_type(
    TypeID type_id,
    TargetTypeCompleteness completeness,
    ReferencePurpose purpose
) noexcept -> void {
    if (purpose == ReferencePurpose::ProviderAccess) {
        // Providers operate on referenced values even through pointers and views.
        // Publish those definitions without turning weak references into layout edges.
        completeness = std::max(completeness, TargetTypeCompleteness::DeferredCompleteDefinition);
    }
    if (!visited_types.emplace(type_id, completeness, purpose).second) {
        return;
    }
    semantic.types().type(type_id).value.visit(
        Overloaded {

            [&](const SliceTypeValue& value) noexcept {
                collect_type(value.element, TargetTypeCompleteness::Declaration, purpose);
            },
            [&](const VectorTypeValue& value) noexcept {
                collect_type(
                    value.element,
                    completeness == TargetTypeCompleteness::Declaration
                        ? TargetTypeCompleteness::Declaration
                        : TargetTypeCompleteness::DeferredCompleteDefinition,
                    purpose
                );
            },
            [&](const RangeTypeValue& value) noexcept {
                collect_type(value.element, TargetTypeCompleteness::Declaration, purpose);
            },
            [&](const PointerTypeValue& value) noexcept {
                collect_type(value.target, TargetTypeCompleteness::Declaration, purpose);
            },
            [&](const CppTypeValue& value) noexcept {
                visit_cpp_type_references(value, [&](TypeID argument) noexcept {
                    collect_type(argument, TargetTypeCompleteness::CompleteDefinition, purpose);
                });
            },
            [](const BuiltinTypeValue&) static noexcept {},
            [&](const StructTypeValue& value) noexcept {
                const auto nominal = NominalDeclarationRef {value.structure};
                require_type(value.structure, completeness);
                if (purpose == ReferencePurpose::ProviderAccess) {
                    if (semantic.declarations().structure(value.structure).kind
                            == RecordKind::Struct
                        && semantic.generic_nominal_instance(nominal) == nullptr) {
                        result.host_nominals.insert(value.structure);
                    }
                    collect_nominal_members(nominal, purpose);
                }
            },
            [&](const EnumTypeValue& value) noexcept {
                const auto nominal = NominalDeclarationRef {value.enumeration};
                require_type(value.enumeration, completeness);
                if (purpose == ReferencePurpose::ProviderAccess) {
                    if (semantic.generic_nominal_instance(nominal) == nullptr) {
                        result.host_nominals.insert(nominal);
                    }
                    collect_nominal_members(nominal, purpose);
                }
            },
            [&](const ArrayTypeValue& value) noexcept {
                collect_type(value.element, completeness, purpose);
            },
            [&](const TupleTypeValue& value) noexcept {
                for (const auto element : value.elements) {
                    collect_type(element, completeness, purpose);
                }
            },
            [&](const OperationTypeValue& value) noexcept {
                collect_type(value.success, completeness, purpose);
                collect_failure_set(value.failures, completeness, purpose);
            },
            [&](const FunctionTypeValue& value) noexcept {
                collect_signature(
                    semantic.declarations().callable(value.callable).signature,
                    completeness == TargetTypeCompleteness::Declaration
                        ? completeness
                        : TargetTypeCompleteness::DeferredCompleteDefinition,
                    purpose
                );
            },
            [&](const ClosureTypeValue& value) noexcept {
                require_type(value.callable, completeness);
                if (purpose != ReferencePurpose::Layout) {
                    result.callables.insert(value.callable);
                    collect_callable(value.callable, purpose);
                    collect_definition(value.callable, purpose);
                }
            },
            [&](const CallableViewTypeValue& value) noexcept {
                collect_signature(
                    value.signature,
                    completeness == TargetTypeCompleteness::Declaration
                        ? completeness
                        : TargetTypeCompleteness::DeferredCompleteDefinition,
                    purpose
                );
            },
        }
    );
}

auto DeclarationReferenceCollector::collect_nominal_members(
    NominalDeclarationRef nominal,
    ReferencePurpose purpose
) noexcept -> void {
    const auto completeness = purpose == ReferencePurpose::ProviderAccess
        ? TargetTypeCompleteness::DeferredCompleteDefinition
        : TargetTypeCompleteness::CompleteDefinition;
    nominal.visit(
        Overloaded {
            [&](StructID id) noexcept {
                for (const auto& field : semantic.declarations().structure(id).fields) {
                    collect_type(field.type, completeness, purpose);
                }
            },
            [&](EnumID id) noexcept {
                const auto& enumeration = semantic.declarations().enumeration(id);
                if (const auto* numeric =
                        std::get_if<NumericEnumRepresentation>(&enumeration.representation)) {
                    collect_type(numeric->underlying_type, completeness, purpose);
                }
                for (const auto case_id : enumeration.cases) {
                    for (const auto type :
                         semantic.declarations().enum_case(case_id).payload_types) {
                        collect_type(type, completeness, purpose);
                    }
                }
            },
        }
    );
}

auto DeclarationReferenceCollector::collect_definition(
    TargetTypeDefinitionRef definition,
    ReferencePurpose purpose
) noexcept -> void {
    definition.visit(
        Overloaded {
            [&](CallableID callable) noexcept {
                const auto body_id = semantic.declarations().body_for_callable(callable);
                const auto& body = semantic.bodies().body(*body_id);
                for (const auto capture : body.inputs().captures) {
                    const auto& binding = body.binding(capture);
                    const auto& storage = std::get<CaptureBindingStorage>(binding.storage);
                    collect_type(
                        binding.type,
                        storage.mode == CaptureMode::Value
                            ? TargetTypeCompleteness::CompleteDefinition
                            : TargetTypeCompleteness::Declaration,
                        purpose
                    );
                }
                collect_signature(
                    semantic.declarations().callable(callable).signature,
                    TargetTypeCompleteness::Declaration,
                    purpose,
                    callable
                );
            },
            [&](auto nominal) noexcept {
                collect_nominal_members(NominalDeclarationRef {nominal}, purpose);
            },
        }
    );
}

auto DeclarationReferenceCollector::collect_body_callable(CallableID callable) noexcept -> void {
    const auto found = module_functions.find(callable);
    if (found == module_functions.end()) {
        return;
    }
    result.callables.insert(callable);
    collect_declaration(found->second);
}

auto DeclarationReferenceCollector::collect_surface(CallableID callable) noexcept -> void {
    if (!visited_surfaces.insert(callable).second) {
        return;
    }
    const auto& surface = semantic.callable_surface(callable);
    for (const auto type : surface.types) {
        collect_type(
            type,
            TargetTypeCompleteness::CompleteDefinition,
            ReferencePurpose::Declaration
        );
    }
    for (const auto reference : surface.callables) {
        collect_body_callable(reference);
    }
}

auto DeclarationReferenceCollector::collect_generic(GenericDeclarationID definition) noexcept
    -> void {
    if (!visited_generics.insert(definition).second) {
        return;
    }
    const auto& dependencies =
        semantic.generic_declaration_contract(definition).implementation_dependencies;
    for (const auto type : dependencies.types) {
        collect_type(
            type,
            TargetTypeCompleteness::CompleteDefinition,
            ReferencePurpose::Declaration
        );
    }
    for (const auto callable : dependencies.callables) {
        collect_body_callable(callable);
    }
    for (const auto dependency : dependencies.definitions) {
        collect_generic(dependency);
    }
}

auto DeclarationReferenceCollector::collect_boundary(FunctionID id) noexcept -> void {
    collect_signature(
        semantic.declarations().callable(semantic.declarations().function(id).callable).signature,
        TargetTypeCompleteness::DeferredCompleteDefinition,
        ReferencePurpose::ProviderAccess
    );
}

auto DeclarationReferenceCollector::collect_declaration(DeclarationRef declaration) noexcept
    -> void {
    declaration.visit(
        Overloaded {
            [&](FunctionID id) noexcept {
                const auto& function = semantic.declarations().function(id);
                collect_signature(
                    semantic.declarations().callable(function.callable).signature,
                    function.cpp_export_origin ? TargetTypeCompleteness::CompleteDefinition
                                               : TargetTypeCompleteness::Declaration,
                    ReferencePurpose::Declaration
                );
                result.callables.insert(function.callable);
                if (function.module_id == surface_module) {
                    collect_surface(function.callable);
                }
            },
            [&](StructID id) noexcept {
                collect_nominal_members(id, ReferencePurpose::Declaration);
            },
            [&](EnumID id) noexcept { collect_nominal_members(id, ReferencePurpose::Declaration); },
            [&](ModuleConstantID id) noexcept {
                const auto type = semantic.constants()
                                      .constant(semantic.declarations().module_constant(id).value)
                                      .type;
                const auto* slice = std::get_if<SliceTypeValue>(&semantic.types().type(type).value);
                collect_type(
                    slice ? slice->element : type,
                    TargetTypeCompleteness::CompleteDefinition,
                    ReferencePurpose::Declaration
                );
            },
        }
    );
}

} // namespace

auto target_visibility(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> DeclarationVisibility {
    return declaration.visit(
        Overloaded {
            [&](FunctionID id) noexcept { return semantic.declarations().function(id).visibility; },
            [&](StructID id) noexcept { return semantic.declarations().structure(id).visibility; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).visibility; },
            [&](ModuleConstantID id) noexcept {
                return semantic.declarations().module_constant(id).visibility;
            },
        }
    );
}

auto target_declaration_ref(NominalDeclarationRef nominal) noexcept -> DeclarationRef {
    return nominal.visit([]<typename ID>(ID id) static noexcept -> DeclarationRef {
        static_assert(std::same_as<ID, StructID> || std::same_as<ID, EnumID>);
        return id;
    });
}

auto target_owner_module(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> ModuleID {
    return declaration.visit(
        Overloaded {
            [&](FunctionID id) noexcept { return semantic.declarations().function(id).module_id; },
            [&](StructID id) noexcept { return semantic.declarations().structure(id).module_id; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).module_id; },
            [&](ModuleConstantID id) noexcept {
                return semantic.declarations().module_constant(id).module_id;
            },
        }
    );
}

auto collect_target_references(
    const SemIRProgram& semantic,
    std::span<const std::vector<DeclarationRef>> surface_declarations,
    std::span<const FunctionID> providers
) noexcept -> TargetReferenceFacts {
    auto result = TargetReferenceFacts {
        .surface_requirements =
            std::vector<std::flat_map<TargetTypeDefinitionRef, TargetTypeCompleteness>>(
                surface_declarations.size()
            ),
        .surface_callables = std::vector<std::flat_set<CallableID>>(surface_declarations.size()),
        .host_nominals = {},
    };
    auto visited = std::vector<bool>(surface_declarations.size());
    for (const auto entry : semantic.declarations().modules()) {
        const auto index = entry.id.index();
        if (index >= surface_declarations.size() || visited[index]) {
            invariant_violation("semantic modules are not a dense owned table");
        }
        visited[index] = true;
        auto collector = DeclarationReferenceCollector(semantic, entry.id);
        for (const auto provider : providers) {
            if (semantic.declarations().function(provider).module_id == entry.id) {
                collector.collect_boundary(provider);
            }
        }
        for (const auto item : semantic.declarations().module_decl(entry.id).items) {
            if (const auto* function = std::get_if<FunctionID>(&item); function != nullptr
                && semantic.declarations().function(*function).cpp_export_origin) {
                collector.collect_boundary(*function);
            }
        }
        for (const auto declaration : surface_declarations[index]) {
            collector.collect_declaration(declaration);
        }
        for (const auto generic : semantic.generic_declaration_contracts()) {
            const auto& contract = generic.value;
            if (contract.module_id == entry.id
                && contract.visibility != DeclarationVisibility::Module) {
                collector.collect_generic(generic.id);
            }
        }
        auto facts = std::move(collector).finish();
        result.surface_requirements[index] = std::move(facts.requirements);
        result.surface_callables[index] = std::move(facts.callables);
        result.host_nominals.insert(facts.host_nominals.begin(), facts.host_nominals.end());
    }
    if (!std::ranges::all_of(visited, std::identity {})) {
        invariant_violation("surface references contain an unknown module row");
    }
    return result;
}

auto target_type_owner(const SemIRProgram& semantic, TargetTypeDefinitionRef definition) noexcept
    -> ModuleID {
    return definition.visit(
        Overloaded {
            [&](CallableID callable) noexcept {
                if (const auto* instance = semantic.generic_callable_instance(callable)) {
                    return semantic.generic_declaration_contract(instance->definition).module_id;
                }
                const auto body = semantic.declarations().body_for_callable(callable);
                const auto source =
                    semantic.provenance()
                        .source_origin(semantic.bodies().body(*body).region().origin)
                        .source_id;
                for (const auto module : semantic.declarations().modules()) {
                    if (semantic.provenance()
                            .module_record(module.value.provenance_module)
                            .source_id
                        == source) {
                        return module.id;
                    }
                }
                invariant_violation("closure definition has no source module");
            },
            [&](auto nominal) noexcept {
                return target_owner_module(semantic, DeclarationRef {nominal});
            },
        }
    );
}

auto target_type_definition_order(
    const SemIRProgram& semantic,
    std::span<const CallableID> closures
) noexcept -> std::vector<TargetTypeDefinitionRef> {
    auto definitions = std::vector<TargetTypeDefinitionRef>();
    for (const auto structure : semantic.declarations().structures()) {
        definitions.push_back(structure.id);
    }
    for (const auto enumeration : semantic.declarations().enumerations()) {
        definitions.push_back(enumeration.id);
    }
    definitions.append_range(closures);
    const auto key = [&](TargetTypeDefinitionRef definition) noexcept {
        const auto owner = target_type_owner(semantic, definition);
        const auto origin = definition.visit(
            Overloaded {
                [&](StructID id) noexcept { return semantic.declarations().structure(id).origin; },
                [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).origin; },
                [&](CallableID id) noexcept {
                    return semantic.bodies()
                        .body(*semantic.declarations().body_for_callable(id))
                        .region()
                        .origin;
                },
            }
        );
        return std::tuple {
            semantic.provenance()
                .module_record(semantic.declarations().module_decl(owner).provenance_module)
                .path.value(),
            semantic.provenance().source_origin(origin).span,
            definition,
        };
    };
    std::ranges::sort(definitions, [&](auto left, auto right) noexcept {
        return key(left) < key(right);
    });
    auto state = std::flat_map<TargetTypeDefinitionRef, std::uint8_t>();
    auto result = std::vector<TargetTypeDefinitionRef>();
    const auto append = [&](this const auto& self,
                            TargetTypeDefinitionRef definition) noexcept -> void {
        if (state[definition] == 2) {
            return;
        }
        if (state[definition] == 1) {
            invariant_violation("target value-type layout is cyclic");
        }
        state[definition] = 1;
        auto collector = DeclarationReferenceCollector(semantic, std::nullopt);
        collector.collect_definition(definition);
        const auto facts = std::move(collector).finish();
        for (const auto& [dependency, completeness] : facts.requirements) {
            if (completeness == TargetTypeCompleteness::CompleteDefinition) {
                self(dependency);
            }
        }
        state[definition] = 2;
        result.push_back(definition);
    };
    for (const auto definition : definitions) {
        append(definition);
    }
    return result;
}
