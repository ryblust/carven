module carven:backend.lowering.context.environment.impl;

import :backend.lowering.context;
import std;

auto ArtifactLowering::require_cpp_environment(
    ModuleID provider,
    CppEnvironmentRequirement requirement
) noexcept -> void {
    static_cast<void>(semantic().declarations().module_decl(provider));
    const auto [entry, inserted] = cpp_environments.try_emplace(provider, requirement);
    if (!inserted && requirement == CppEnvironmentRequirement::Using) {
        entry->second = requirement;
    }
}

auto ArtifactLowering::materialize_cpp_environments(
    TargetUnitSections& sections,
    TargetDirectiveInputs& directives
) noexcept -> void {
    auto imports = std::vector<TargetItem>();
    for (const auto& [provider, requirement] : cpp_environments) {
        auto bindings = std::vector<TargetItem>();
        for (const auto& header : semantic().declarations().module_decl(provider).cpp_headers) {
            const auto name = semantic().provenance().spelling(header.name);
            directives.prefix_groups.push_back(
                {.directives =
                     {{.bytes = header.delimiter == CppHeaderDelimiter::AngleBrackets
                           ? std::format("#include <{}>", name)
                           : std::format("#include \"{}\"", name)}},
                 .attribution = TargetRawSourceAttribution {
                     .origin = target_source_origin(semantic().provenance(), header.origin),
                 }}
            );
            if (requirement == CppEnvironmentRequirement::Declarations) {
                continue;
            }
            for (const auto& binding : header.bindings) {
                auto components = std::vector<TargetIdentifier>();
                for (const auto component : binding.components) {
                    components.push_back(
                        TargetIdentifier::from_spelling(semantic().provenance().spelling(component))
                    );
                }
                bindings.push_back(source_item(
                    semantic(),
                    binding.origin,
                    TargetUsing {
                        .name = TargetName::globally_qualified(std::move(components)),
                        .opens_namespace = binding.opens_namespace
                    }
                ));
            }
        }
        if (!bindings.empty()) {
            imports.push_back(namespace_item(
                plan().names().module_names(provider).qualified_namespace_name,
                std::move(bindings)
            ));
        }
    }
    sections.preamble.insert(
        sections.preamble.end(),
        std::make_move_iterator(imports.begin()),
        std::make_move_iterator(imports.end())
    );
}
