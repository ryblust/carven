module carven:semantic.visibility.impl;

import :semantic.visibility;
import :support.visit;
import std;

auto declaration_audience(
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    CompilationProvenanceView provenance
) noexcept -> DeclarationAudience {
    switch (visibility) {
        case DeclarationVisibility::Module: return ModuleAudience {.module_id = defining_module};
        case DeclarationVisibility::ModuleDomain:
            return ModuleDomainAudience {
                .prefix = provenance.module_record(defining_module).path.module_domain_prefix(),
            };
        case DeclarationVisibility::Compilation: return CompilationAudience {};
    }
    std::unreachable();
}

auto audience_subset_of(
    const DeclarationAudience& left,
    const DeclarationAudience& right,
    CompilationProvenanceView provenance
) noexcept -> bool {
    return std::visit(
        Overloaded {
            [](const ModuleAudience& left_value,
               const ModuleAudience& right_value) static noexcept {
                return left_value.module_id == right_value.module_id;
            },
            [&](const ModuleAudience& left_value,
                const ModuleDomainAudience& right_value) noexcept {
                return provenance.module_record(left_value.module_id).path.module_domain_prefix()
                    == right_value.prefix;
            },
            [](const ModuleAudience&, const CompilationAudience&) static noexcept { return true; },
            [](const ModuleDomainAudience& left_value,
               const ModuleDomainAudience& right_value) static noexcept {
                return left_value.prefix == right_value.prefix;
            },
            [](const ModuleDomainAudience&, const CompilationAudience&) static noexcept {
                return true;
            },
            [](const CompilationAudience&, const CompilationAudience&) static noexcept {
                return true;
            },
            [](const ModuleDomainAudience&, const ModuleAudience&) static noexcept {
                return false;
            },
            [](const CompilationAudience&, const ModuleAudience&) static noexcept { return false; },
            [](const CompilationAudience&, const ModuleDomainAudience&) static noexcept {
                return false;
            },
        },
        left,
        right
    );
}

auto declaration_visible_to(
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    ProgramModuleID importer,
    CompilationProvenanceView provenance
) noexcept -> bool {
    switch (visibility) {
        case DeclarationVisibility::Module: return defining_module == importer;
        case DeclarationVisibility::ModuleDomain:
            return same_module_domain(
                provenance.module_record(defining_module).path,
                provenance.module_record(importer).path
            );
        case DeclarationVisibility::Compilation: return true;
    }
    std::unreachable();
}
