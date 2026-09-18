module carven:backend.lowering.constant.storage.impl;

import :backend.generation.names;
import :backend.lowering.constant.storage;
import :backend.lowering.context;
import :backend.target.decl;
import :support.invariant;
import std;

ConstantStorage::ConstantStorage(
    const PlannedCompilation& compilation,
    TargetArtifactID artifact
) noexcept
    : compilation(compilation),
      artifact_id(artifact) {
    if (const auto* implementation = std::get_if<TargetModuleImplementationArtifact>(
            &compilation.target().artifact(artifact)
        )) {
        storage_namespace = TargetNameAllocator::artifact_storage_namespace(
            artifact.index(),
            compilation.target()
                .names()
                .module_names(implementation->schedule.module_id)
                .reserved_identifiers
        );
    }
}

auto ConstantStorage::find(ConstantID id) const noexcept -> std::optional<TargetName> {
    const auto found = names.find(id);
    return found == names.end() ? std::nullopt : std::optional(found->second);
}

auto ConstantStorage::empty() const noexcept -> bool {
    return items.empty();
}

auto ConstantStorage::append(ConstantID id, TargetTypeID type, TargetExpr initializer) noexcept
    -> TargetName {
    static_cast<void>(compilation.semantic().constants().constant(id));
    if (names.contains(id)) {
        invariant_violation("constant storage was materialized more than once in an artifact");
    }
    const auto* implementation = std::get_if<TargetModuleImplementationArtifact>(
        &compilation.target().artifact(artifact_id)
    );
    if (implementation == nullptr) {
        invariant_violation("constant storage requires a module implementation artifact");
    }
    const auto identifier = TargetNameAllocator::constant_storage_identifier(id.index());
    auto components = std::vector<TargetIdentifier>();
    for (const auto& component : compilation.target().names().generated_namespace().components()) {
        components.push_back(component);
    }
    for (const auto& component : compilation.target().names().domain_namespace().components()) {
        components.push_back(component);
    }
    for (const auto& component : compilation.target()
                                     .names()
                                     .module_names(implementation->schedule.module_id)
                                     .module_namespace_name.components()) {
        components.push_back(component);
    }
    components.push_back(*storage_namespace);
    components.push_back(identifier);
    auto name = TargetName::globally_qualified(std::move(components));
    names.emplace(id, name);
    items.push_back(target_lowering_item(
        TargetDecl {TargetVariableDecl {
            .name = identifier,
            .type = type,
            .initializer = std::move(initializer),
            .inline_specifier = true,
            .constexpr_specifier = true,
        }}
    ));
    return name;
}

auto ConstantStorage::take() noexcept -> std::vector<TargetItem> {
    if (items.empty()) {
        return {};
    }
    const auto* implementation = std::get_if<TargetModuleImplementationArtifact>(
        &compilation.target().artifact(artifact_id)
    );
    if (implementation == nullptr) {
        invariant_violation("constant storage requires a module implementation artifact");
    }
    auto storage = namespace_item(
        TargetName(*storage_namespace),
        std::exchange(items, {}),
        TargetCompilerReason::ArtifactScaffolding,
        false
    );
    auto module_namespace = namespace_item(
        compilation.target()
            .names()
            .module_names(implementation->schedule.module_id)
            .module_namespace_name,
        target_items(std::move(storage)),
        TargetCompilerReason::ArtifactScaffolding,
        false
    );
    auto domain = namespace_item(
        compilation.target().names().domain_namespace(),
        target_items(std::move(module_namespace)),
        TargetCompilerReason::ArtifactScaffolding,
        false
    );
    return target_items(namespace_item(
        compilation.target().names().generated_namespace(),
        target_items(std::move(domain))
    ));
}
