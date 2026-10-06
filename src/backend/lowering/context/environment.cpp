module carven:backend.lowering.context.environment.impl;

import :backend.lowering.context;
import :backend.target.header;
import :backend.target.unit;
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

auto ArtifactLowering::lower_cpp_environments(
    TargetUnitSections& sections,
    TargetDirectiveInputs& directives
) noexcept -> void {
    auto imports = std::vector<TargetItem>();
    auto providers = cpp_environments | std::views::keys | std::ranges::to<std::vector>();
    std::ranges::sort(providers, {}, [&](ModuleID id) noexcept {
        const auto& declaration = semantic().declarations().module_decl(id);
        return semantic().provenance().module_record(declaration.provenance_module).path.value();
    });
    for (const auto provider : providers) {
        const auto requirement = cpp_environments.at(provider);
        auto bindings = std::vector<TargetItem>();
        const auto& declaration = semantic().declarations().module_decl(provider);
        for (const auto& header : declaration.cpp_headers) {
            const auto name = semantic().provenance().spelling(header.name);
            directives.native_headers.push_back({
                .header =
                    {
                        .delimiter = header.delimiter == CppHeaderDelimiter::AngleBrackets
                            ? TargetHeaderDelimiter::AngleBrackets
                            : TargetHeaderDelimiter::Quotes,
                        .path = std::string(name),
                    },
                .attribution = TargetRawSourceAttribution {
                    .origin = target_source_origin(semantic().provenance(), header.origin),
                },
            });
            if (requirement == CppEnvironmentRequirement::Declarations) {
                continue;
            }
            if (header.namespace_opening) {
                const auto& binding = *header.namespace_opening;
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
                        .opens_namespace = true
                    }
                ));
            }
        }
        if (!bindings.empty()) {
            imports.push_back(namespace_item(
                plan().names().module_names(provider).qualified_namespace_name,
                std::move(bindings),
                TargetCompilerReason::ArtifactScaffolding,
                false
            ));
        }
    }
    sections.preamble.insert(
        sections.preamble.end(),
        std::make_move_iterator(imports.begin()),
        std::make_move_iterator(imports.end())
    );
}
