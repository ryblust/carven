module carven:backend.generation.program.impl;

import :artifacts;
import :backend.generation.linkage;
import :backend.generation.program;
import :backend.generation.program.construction;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto logical_path(
    std::string_view prefix,
    std::span<const std::string> components,
    std::string_view extension
) noexcept -> std::string {
    auto result = std::string(prefix);
    for (const auto& component : components) {
        if (!result.empty()) {
            result += '/';
        }
        result += component;
    }
    result += extension;
    return result;
}

} // namespace

auto module_implementation_logical_path(std::span<const std::string> canonical_components) noexcept
    -> std::string {
    return logical_path({}, canonical_components, ".cpp");
}

auto interface_component_logical_path(std::span<const std::string> anchor_components) noexcept
    -> std::string {
    return logical_path("carven/generated", anchor_components, ".hpp");
}

TargetProgramBuilder::TargetProgramBuilder(
    SemanticProgram source,
    TargetGenerationRequest generation_request
) noexcept
    : semantic(std::move(source)),
      request(std::move(generation_request)),
      linkage_domain(derive_linkage_domain_id(request)) {}

auto TargetProgramBuilder::finish() && noexcept -> TargetProgram {
    if (semantic.modules().empty()) {
        invariant_violation("target program construction requires at least one semantic module");
    }
    allocate_names();
    derive_representations();
    derive_value_binding_requirements();
    build_artifacts();
    if (!name_allocation.has_value()) {
        invariant_violation("target program names were not completed");
    }
    verify();
    return TargetProgram(
        std::move(semantic),
        std::move(name_allocation->modules),
        std::move(artifacts),
        std::move(types),
        std::move(failure_profiles),
        std::move(call_signatures),
        std::move(callable_signatures),
        std::move(carrier_shapes),
        std::move(carrier_index),
        std::move(carrier_conversions),
        std::move(mutable_value_binding_flags),
        std::move(name_allocation->generated_namespace),
        std::move(name_allocation->domain_namespace),
        std::move(name_allocation->entities),
        std::move(name_allocation->payload_enums),
        std::move(name_allocation->scoped_source_names)
    );
}

auto TargetProgramBuilder::verify() const noexcept -> void {
    if (!name_allocation.has_value()
        || name_allocation->modules.size() != semantic.modules().size()
        || name_allocation->entities.size() != semantic.symbols().size()
        || name_allocation->payload_enums.size() != semantic.enumerations().size()
        || name_allocation->scoped_source_names.size() != semantic.scopes().size()
        || types.size() != semantic.types().size()
        || failure_profiles.size() != semantic.failure_sets().size()
        || callable_signatures.size() != semantic.callables().size()
        || function_reference_signatures.size() != semantic.callable_signatures().size()
        || mutable_value_binding_flags.size() != semantic.symbols().size()) {
        invariant_violation("target program is not total over its semantic key domains");
    }
    if (carrier_conversions.size() != failure_profiles.size() * failure_profiles.size()) {
        invariant_violation("target carrier conversion table has an invalid shape");
    }
    for (auto source = 0uz; source < failure_profiles.size(); ++source) {
        for (auto destination = 0uz; destination < failure_profiles.size(); ++destination) {
            const auto encoded =
                carrier_conversions[source * failure_profiles.size() + destination];
            if (source == destination) {
                if (encoded != static_cast<std::uint8_t>(TargetCarrierConversion::Identity)) {
                    invariant_violation("target carrier conversion identity law is incomplete");
                }
                continue;
            }
            if (encoded == static_cast<std::uint8_t>(TargetCarrierConversion::Identity)) {
                invariant_violation("target carrier conversion aliases distinct profiles");
            }
            if (encoded == static_cast<std::uint8_t>(TargetCarrierConversion::Widen)
                && failure_profiles[source].ordered_members.size()
                    >= failure_profiles[destination].ordered_members.size()) {
                invariant_violation("target carrier widening is not a strict profile expansion");
            }
        }
    }
    for (const auto& profile : failure_profiles) {
        if (std::ranges::any_of(profile.ordered_members, [&](HIRTypeID failure) noexcept {
                return failure.index() >= types.size();
            })) {
            invariant_violation("target failure profile references an unknown type recipe");
        }
    }
    for (const auto& type : types) {
        const auto valid = std::visit(
            Overloaded {
                [](const TargetIntrinsicTypeRecipe&) static noexcept { return true; },
                [&](const TargetNamedTypeRecipe& value) noexcept {
                    return value.symbol.index() < name_allocation->entities.size()
                        && name_allocation->entities[value.symbol.index()].has_value();
                },
                [&](const TargetArrayTypeRecipe& value) noexcept {
                    return value.element.index() < types.size();
                },
                [&](const TargetFunctionReferenceTypeRecipe& value) noexcept {
                    return value.signature.index() < call_signatures.size();
                },
                [&](const TargetCallableTypeRecipe& value) noexcept {
                    return value.signature.index() < call_signatures.size();
                },
                [](const TargetDeducedTypeRecipe&) static noexcept { return true; },
            },
            type.value
        );
        if (!valid) {
            invariant_violation("target type recipe references an unknown sealed identity");
        }
    }
    for (auto index = 0uz; index < semantic.enumerations().size(); ++index) {
        const auto enumeration = EnumID::from_index(static_cast<std::uint32_t>(index));
        const auto requires_payload_names =
            semantic.enumeration(enumeration).profile == HIREnumProfile::Payload;
        if (name_allocation->payload_enums[index].has_value() != requires_payload_names) {
            invariant_violation("target payload-enum names are not total");
        }
    }
    const auto valid_declaration = [&](HIRDeclarationRef declaration) noexcept {
        return std::visit(
            Overloaded {
                [&](FunctionID id) noexcept { return id.index() < semantic.functions().size(); },
                [&](StructID id) noexcept { return id.index() < semantic.structures().size(); },
                [&](EnumID id) noexcept { return id.index() < semantic.enumerations().size(); },
            },
            declaration
        );
    };
    const auto valid_nominal = [&](HIRNominalDeclRef declaration) noexcept {
        return std::visit(
            Overloaded {
                [&](StructID id) noexcept { return id.index() < semantic.structures().size(); },
                [&](EnumID id) noexcept { return id.index() < semantic.enumerations().size(); },
            },
            declaration
        );
    };
    auto logical_paths = std::flat_set<std::string_view>();
    for (auto index = 0uz; index < artifacts.size(); ++index) {
        const auto& artifact = artifacts[index];
        if (!validate_artifact_logical_path(artifact.logical_path)
            || !logical_paths.insert(artifact.logical_path).second) {
            invariant_violation("target artifact graph contains an invalid logical path");
        }
        if (artifact.directive_groups.empty()) {
            invariant_violation("target artifact graph has no directive groups");
        }
        auto has_pragma_once = false;
        auto dependencies = std::flat_set<TargetArtifactID>();
        for (const auto& group : artifact.directive_groups) {
            if (group.directives.empty()) {
                invariant_violation("target artifact graph contains an empty directive group");
            }
            for (const auto& directive : group.directives) {
                std::visit(
                    Overloaded {
                        [&](const TargetDirective& value) noexcept {
                            if (value.bytes.empty() || !value.bytes.starts_with('#')) {
                                invariant_violation(
                                    "target artifact graph contains an invalid directive"
                                );
                            }
                            has_pragma_once |= value.bytes == "#pragma once";
                        },
                        [&](const TargetArtifactIncludeDirective& value) noexcept {
                            if (value.artifact.index() >= index
                                || !dependencies.insert(value.artifact).second) {
                                invariant_violation(
                                    "target artifact graph has an invalid typed dependency"
                                );
                            }
                        },
                    },
                    directive
                );
            }
        }
        const auto schedule_role = std::visit(
            Overloaded {
                [](const TargetInterfaceSchedule&) static noexcept {
                    return GeneratedArtifactRole::Interface;
                },
                [](const TargetModuleSchedule&) static noexcept {
                    return GeneratedArtifactRole::ModuleImplementation;
                },
                [](const TargetTestEntrySchedule&) static noexcept {
                    return GeneratedArtifactRole::TestEntry;
                },
            },
            artifact.schedule
        );
        if (schedule_role != artifact.role) {
            invariant_violation("target artifact role disagrees with its schedule");
        }
        const auto schedule_valid = std::visit(
            Overloaded {
                [&](const TargetInterfaceSchedule& schedule) noexcept {
                    auto members = std::flat_set<ProgramModuleID>();
                    if (schedule.component_members.empty()
                        || std::ranges::any_of(
                            schedule.component_members,
                            [&](ProgramModuleID module) noexcept {
                                return module.index() >= semantic.modules().size()
                                    || !members.insert(module).second;
                            }
                        )) {
                        return false;
                    }
                    return std::ranges::all_of(
                               schedule.forward_declarations,
                               [&](const TargetInterfaceForwardDeclaration& declaration) noexcept {
                                   return declaration.module_id.index() < semantic.modules().size()
                                       && valid_nominal(declaration.declaration);
                               }
                           )
                        && std::ranges::all_of(
                               schedule.declarations,
                               [&](const TargetInterfaceDeclaration& declaration) noexcept {
                                   return members.contains(declaration.module_id)
                                       && valid_declaration(declaration.declaration);
                               }
                        );
                },
                [&](const TargetModuleSchedule& schedule) noexcept {
                    if (schedule.module_id.index() >= semantic.modules().size()) {
                        return false;
                    }
                    const auto& items = semantic.hir_module(schedule.module_id).items;
                    const auto valid_function = [&](FunctionID function) noexcept {
                        return function.index() < semantic.functions().size();
                    };
                    return std::ranges::all_of(schedule.implementation_nominal_order, valid_nominal)
                        && std::ranges::all_of(
                               schedule.cpp_preamble_items,
                               [&](std::uint32_t item) noexcept {
                                   return item < items.size()
                                       && std::holds_alternative<HIRCppRegion>(items[item]);
                               }
                        )
                        && std::ranges::all_of(
                               schedule.private_function_declarations,
                               valid_function
                        )
                        && std::ranges::all_of(schedule.function_definitions, valid_function)
                        && (!schedule.entry_point.has_value()
                            || valid_function(*schedule.entry_point))
                        && std::ranges::all_of(schedule.emitted_tests, [&](TestID test) noexcept {
                               return test.index() < semantic.tests().size();
                           });
                },
                [](const TargetTestEntrySchedule&) static noexcept { return true; },
            },
            artifact.schedule
        );
        if (!schedule_valid) {
            invariant_violation("target artifact contains an invalid declaration schedule");
        }
        const auto stable = artifact.source_mapping == ArtifactSourceMappingPolicy::StableInterface;
        if (stable != (artifact.role == GeneratedArtifactRole::Interface)) {
            invariant_violation("target artifact role disagrees with source mapping policy");
        }
        if (has_pragma_once != stable) {
            invariant_violation("target artifact role disagrees with directive policy");
        }
    }
    for (const auto path : logical_paths) {
        for (auto separator = path.find('/'); separator != std::string_view::npos;
             separator = path.find('/', separator + 1)) {
            if (logical_paths.contains(path.substr(0, separator))) {
                invariant_violation("target artifact path descends from an artifact file");
            }
        }
    }
    for (const auto& signature : call_signatures) {
        if (signature.result.index() >= types.size()
            || signature.failure_profile.index() >= failure_profiles.size()
            || std::ranges::any_of(
                signature.parameters,
                [&](const TargetCallParameterRecipe& parameter) noexcept {
                    return parameter.type.index() >= types.size();
                }
            )) {
            invariant_violation("target call signature references an unknown recipe");
        }
        if (signature.carrier_shape.has_value()
            && signature.carrier_shape->index() >= carrier_shapes.size()) {
            invariant_violation("target call signature references an unknown carrier shape");
        }
        if (signature.carrier_shape.has_value()) {
            const auto& shape = carrier_shapes[signature.carrier_shape->index()];
            if (shape.result != signature.result
                || shape.failure_profile != signature.failure_profile) {
                invariant_violation("target call signature carrier shape disagrees with its ABI");
            }
        }
    }
    if (std::ranges::any_of(callable_signatures, [&](TargetCallSignatureID signature) noexcept {
            return signature.index() >= call_signatures.size();
        })) {
        invariant_violation("target callable mapping references an unknown signature recipe");
    }
    for (auto index = 0uz; index < carrier_shapes.size(); ++index) {
        const auto& shape = carrier_shapes[index];
        if (shape.result.index() >= types.size()
            || shape.failure_profile.index() >= failure_profiles.size()
            || failure_profiles[shape.failure_profile.index()].ordered_members.empty()) {
            invariant_violation("target carrier shape references an invalid representation");
        }
        const auto found =
            carrier_index.find({shape.result.index(), shape.failure_profile.index()});
        if (found == carrier_index.end() || found->second.index() != index) {
            invariant_violation("target carrier shape index is not canonical");
        }
    }
}

TargetProgram::TargetProgram(
    SemanticProgram semantic,
    std::vector<TargetModuleNames> modules,
    std::vector<TargetArtifactSpec> artifacts,
    std::vector<TargetTypeRecipe> types,
    std::vector<TargetFailureProfile> failure_profiles,
    std::vector<TargetCallSignatureRecipe> call_signatures,
    std::vector<TargetCallSignatureID> callable_signatures,
    std::vector<TargetCarrierShape> carrier_shapes,
    std::flat_map<std::pair<std::uint32_t, std::uint32_t>, TargetCarrierShapeID> carrier_index,
    std::vector<std::uint8_t> carrier_conversions,
    std::vector<std::uint8_t> mutable_value_binding_flags,
    TargetName generated_namespace,
    TargetName domain_namespace,
    std::vector<std::optional<TargetEntityName>> entity_names,
    std::vector<std::optional<TargetPayloadEnumNames>> payload_enums,
    std::vector<std::flat_set<std::string>> scoped_source_names
) noexcept
    : semantic_program(std::move(semantic)),
      target_modules(std::move(modules)),
      target_artifacts(std::move(artifacts)),
      target_types(std::move(types)),
      target_failure_profiles(std::move(failure_profiles)),
      target_call_signatures(std::move(call_signatures)),
      target_callable_signatures(std::move(callable_signatures)),
      target_carrier_shapes(std::move(carrier_shapes)),
      target_carrier_index(std::move(carrier_index)),
      target_carrier_conversions(std::move(carrier_conversions)),
      mutable_value_binding_flags(std::move(mutable_value_binding_flags)),
      target_generated_namespace(std::move(generated_namespace)),
      target_domain_namespace(std::move(domain_namespace)),
      target_entity_names(std::move(entity_names)),
      target_payload_enums(std::move(payload_enums)),
      target_scoped_source_names(std::move(scoped_source_names)) {}

auto TargetProgram::build(SemanticProgram semantic, TargetGenerationRequest request) noexcept
    -> TargetProgram {
    return TargetProgramBuilder(std::move(semantic), std::move(request)).finish();
}

auto TargetProgram::artifacts() const noexcept -> std::span<const TargetArtifactSpec> {
    return target_artifacts;
}

auto TargetProgram::artifact(TargetArtifactID id) const noexcept -> const TargetArtifactSpec& {
    if (id.index() >= target_artifacts.size()) {
        invariant_violation("target program references an unknown artifact");
    }
    return target_artifacts[id.index()];
}

auto TargetProgram::focused_artifact(TargetArtifactID id) const noexcept -> TargetArtifactView {
    static_cast<void>(artifact(id));
    return TargetArtifactView(*this, id);
}

auto TargetProgram::resolve_name(
    ProgramModuleID active_module,
    const TargetEntityName& name
) const noexcept -> TargetName {
    auto components = std::vector<TargetIdentifier>(
        name.relative_name.components().begin(),
        name.relative_name.components().end()
    );
    if (name.owner_module == active_module) {
        return TargetName::from_components(std::move(components));
    }
    if (name.owner_module.index() >= target_modules.size()) {
        invariant_violation("target entity name has an unknown owner module");
    }
    const auto& target_namespace =
        target_modules[name.owner_module.index()].qualified_namespace_name.components();
    auto qualified =
        std::vector<TargetIdentifier>(target_namespace.begin(), target_namespace.end());
    qualified.insert(qualified.end(), components.begin(), components.end());
    return TargetName::globally_qualified(std::move(qualified));
}

TargetArtifactView::TargetArtifactView(
    const TargetProgram& program,
    TargetArtifactID artifact_id
) noexcept
    : target_program(std::addressof(program)),
      focused_artifact_id(artifact_id) {}

auto TargetArtifactView::artifact() const noexcept -> const TargetArtifactSpec& {
    return target_program->artifact(focused_artifact_id);
}

auto TargetArtifactView::materialize_directive_groups() const noexcept
    -> std::vector<TargetDirectiveGroup> {
    auto result = std::vector<TargetDirectiveGroup>();
    result.reserve(artifact().directive_groups.size());
    for (const auto& group : artifact().directive_groups) {
        auto directives = std::vector<TargetDirective>();
        directives.reserve(group.directives.size());
        for (const auto& directive : group.directives) {
            directives.push_back(
                std::visit(
                    Overloaded {
                        [](const TargetDirective& value) static noexcept { return value; },
                        [&](const TargetArtifactIncludeDirective& value) noexcept {
                            return TargetDirective {
                                .bytes = std::format(
                                    "#include <{}>",
                                    target_program->artifact(value.artifact).logical_path
                                ),
                            };
                        },
                    },
                    directive
                )
            );
        }
        result.push_back({.directives = std::move(directives)});
    }
    return result;
}

auto TargetArtifactView::module(ProgramModuleID module_id) const noexcept
    -> const TargetModuleNames& {
    if (module_id.index() >= target_program->target_modules.size()) {
        invariant_violation("target artifact view references an unknown module");
    }
    return target_program->target_modules[module_id.index()];
}

auto TargetArtifactView::module_count() const noexcept -> std::size_t {
    return target_program->target_modules.size();
}

auto TargetArtifactView::type_count() const noexcept -> std::size_t {
    return target_program->target_types.size();
}

auto TargetArtifactView::generated_namespace() const noexcept -> const TargetName& {
    return target_program->target_generated_namespace;
}

auto TargetArtifactView::domain_namespace() const noexcept -> const TargetName& {
    return target_program->target_domain_namespace;
}

auto TargetArtifactView::entity_identifier(SymbolID symbol) const noexcept
    -> const TargetIdentifier& {
    if (symbol.index() >= target_program->target_entity_names.size()
        || !target_program->target_entity_names[symbol.index()].has_value()) {
        invariant_violation("target artifact view references an unnamed symbol");
    }
    return target_program->target_entity_names[symbol.index()]->relative_name.components().back();
}

auto TargetArtifactView::entity_name(ProgramModuleID active_module, SymbolID symbol) const noexcept
    -> TargetName {
    if (symbol.index() >= target_program->target_entity_names.size()
        || !target_program->target_entity_names[symbol.index()].has_value()) {
        invariant_violation("target artifact view references an unnamed symbol");
    }
    return target_program->resolve_name(
        active_module,
        *target_program->target_entity_names[symbol.index()]
    );
}

auto TargetArtifactView::payload_enum(EnumID enumeration) const noexcept
    -> const TargetPayloadEnumNames& {
    if (enumeration.index() >= target_program->target_payload_enums.size()
        || !target_program->target_payload_enums[enumeration.index()].has_value()) {
        invariant_violation("target artifact view references a non-payload enum");
    }
    return *target_program->target_payload_enums[enumeration.index()];
}

auto TargetArtifactView::source_names(SemanticScopeID scope) const noexcept
    -> const std::flat_set<std::string>& {
    if (scope.index() >= target_program->target_scoped_source_names.size()) {
        invariant_violation("target artifact view references an unknown semantic scope");
    }
    return target_program->target_scoped_source_names[scope.index()];
}

auto TargetArtifactView::type_recipe(HIRTypeID id) const noexcept -> const TargetTypeRecipe& {
    if (id.index() >= target_program->target_types.size()) {
        invariant_violation("target artifact view references an unknown type recipe");
    }
    return target_program->target_types[id.index()];
}

auto TargetArtifactView::failure_profile(FailureSetID id) const noexcept
    -> const TargetFailureProfile& {
    if (id.index() >= target_program->target_failure_profiles.size()) {
        invariant_violation("target artifact view references an unknown failure profile");
    }
    return target_program->target_failure_profiles[id.index()];
}

auto TargetArtifactView::callable_signature(CallableID id) const noexcept
    -> const TargetCallSignatureRecipe& {
    if (id.index() >= target_program->target_callable_signatures.size()) {
        invariant_violation("target artifact view references an unknown callable signature");
    }
    return target_program
        ->target_call_signatures[target_program->target_callable_signatures[id.index()].index()];
}

auto TargetArtifactView::call_signature(TargetCallSignatureID id) const noexcept
    -> const TargetCallSignatureRecipe& {
    if (id.index() >= target_program->target_call_signatures.size()) {
        invariant_violation("target artifact view references an unknown target call signature");
    }
    return target_program->target_call_signatures[id.index()];
}

auto TargetArtifactView::carrier_shape(
    HIRTypeID result,
    FailureSetID failure_profile
) const noexcept -> TargetCarrierShapeID {
    const auto found =
        target_program->target_carrier_index.find({result.index(), failure_profile.index()});
    if (found == target_program->target_carrier_index.end()) {
        invariant_violation("target lowering requested an unsealed carrier shape");
    }
    return found->second;
}

auto TargetArtifactView::carrier_shape(TargetCarrierShapeID id) const noexcept
    -> const TargetCarrierShape& {
    if (id.index() >= target_program->target_carrier_shapes.size()) {
        invariant_violation("target artifact view references an unknown carrier shape");
    }
    return target_program->target_carrier_shapes[id.index()];
}

auto TargetArtifactView::classify_failure_profile_conversion(
    FailureSetID source,
    FailureSetID destination
) const noexcept -> TargetCarrierConversion {
    const auto profile_count = target_program->target_failure_profiles.size();
    if (source.index() >= profile_count || destination.index() >= profile_count) {
        invariant_violation("carrier conversion references an unknown failure profile");
    }
    const auto encoded =
        target_program
            ->target_carrier_conversions[source.index() * profile_count + destination.index()];
    if (encoded == static_cast<std::uint8_t>(TargetCarrierConversion::Identity)) {
        return TargetCarrierConversion::Identity;
    }
    if (encoded == static_cast<std::uint8_t>(TargetCarrierConversion::Widen)) {
        return TargetCarrierConversion::Widen;
    }
    invariant_violation("failure-profile destination does not cover the source failures");
}

auto TargetArtifactView::classify_carrier_conversion(
    TargetCarrierShapeID source,
    TargetCarrierShapeID destination
) const noexcept -> TargetCarrierConversion {
    const auto& source_shape = carrier_shape(source);
    const auto& destination_shape = carrier_shape(destination);
    if (source_shape.result != destination_shape.result) {
        invariant_violation("target carrier conversion changes the result recipe");
    }
    return classify_failure_profile_conversion(
        source_shape.failure_profile,
        destination_shape.failure_profile
    );
}

auto TargetArtifactView::requires_mutable_value_binding(SymbolID symbol) const noexcept -> bool {
    if (symbol.index() >= target_program->mutable_value_binding_flags.size()) {
        invariant_violation("target artifact view references an unknown symbol binding");
    }
    return target_program->mutable_value_binding_flags[symbol.index()] != 0u;
}

auto TargetArtifactView::provenance() const noexcept -> CompilationProvenanceView {
    return target_program->semantic_program.provenance();
}
auto TargetArtifactView::type(HIRTypeID id) const noexcept -> const HIRType& {
    return target_program->semantic_program.type(id);
}
auto TargetArtifactView::expression(HIRExprID id) const noexcept -> const HIRExpr& {
    return target_program->semantic_program.expression(id);
}
auto TargetArtifactView::expression_control(HIRExprID id) const noexcept
    -> const HIRExpressionControl& {
    return target_program->semantic_program.expression_control(id);
}
auto TargetArtifactView::evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect& {
    return target_program->semantic_program.evaluation_effect(id);
}
auto TargetArtifactView::try_facts(HIRExprID id) const noexcept
    -> const std::optional<HIRTryFacts>& {
    return target_program->semantic_program.try_facts(id);
}
auto TargetArtifactView::constant(HIRConstantID id) const noexcept -> const HIRConstantFact& {
    return target_program->semantic_program.constant(id);
}
auto TargetArtifactView::statement(HIRStmtID id) const noexcept -> const HIRStmt& {
    return target_program->semantic_program.statement(id);
}
auto TargetArtifactView::pattern(HIRPatternID id) const noexcept -> const HIRPattern& {
    return target_program->semantic_program.pattern(id);
}
auto TargetArtifactView::block(HIRBlockID id) const noexcept -> const HIRBlock& {
    return target_program->semantic_program.block(id);
}
auto TargetArtifactView::block_control(HIRBlockID id) const noexcept -> const HIRBlockControl& {
    return target_program->semantic_program.block_control(id);
}
auto TargetArtifactView::binding(SymbolID id) const noexcept
    -> const std::optional<SemanticBindingFacts>& {
    return target_program->semantic_program.binding(id);
}
auto TargetArtifactView::function(FunctionID id) const noexcept -> const HIRFunctionDecl& {
    return target_program->semantic_program.function(id);
}
auto TargetArtifactView::body(BodyID id) const noexcept -> const HIRBody& {
    return target_program->semantic_program.body(id);
}
auto TargetArtifactView::test(TestID id) const noexcept -> const HIRTestDecl& {
    return target_program->semantic_program.test(id);
}
auto TargetArtifactView::structure(StructID id) const noexcept -> const HIRStructDecl& {
    return target_program->semantic_program.structure(id);
}
auto TargetArtifactView::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return target_program->semantic_program.enumeration(id);
}
auto TargetArtifactView::enum_case(EnumCaseID id) const noexcept -> const HIREnumCase& {
    return target_program->semantic_program.enum_case(id);
}
auto TargetArtifactView::nominal_capabilities(HIRNominalDeclRef id) const noexcept
    -> const HIRNominalCapabilities& {
    return target_program->semantic_program.nominal_capabilities(id);
}
auto TargetArtifactView::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return target_program->semantic_program.symbol(id);
}
auto TargetArtifactView::hir_module(ProgramModuleID id) const noexcept -> const HIRModule& {
    return target_program->semantic_program.hir_module(id);
}
auto TargetArtifactView::callable(CallableID id) const noexcept -> const HIRCallable& {
    return target_program->semantic_program.callable(id);
}
