module carven:semantic.visibility;

import :source.module_path;
import :source.provenance;
import std;

enum class DeclarationVisibility {
    Module,
    ModuleDomain,
    Compilation,
};

struct ModuleAudience final {
    ProgramModuleID module_id;
};

struct ModuleDomainAudience final {
    ModuleDomainPrefix prefix;
};

struct CompilationAudience final {};

using DeclarationAudience = std::variant<ModuleAudience, ModuleDomainAudience, CompilationAudience>;

auto declaration_audience(
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    CompilationProvenanceView provenance
) noexcept -> DeclarationAudience;

auto audience_subset_of(
    const DeclarationAudience& left,
    const DeclarationAudience& right,
    CompilationProvenanceView provenance
) noexcept -> bool;

auto declaration_visible_to(
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    ProgramModuleID importer,
    CompilationProvenanceView provenance
) noexcept -> bool;
