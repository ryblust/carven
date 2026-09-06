module carven:semantic.analysis.names.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.names;
import :semantic.analysis.program;
import :source.cpp.identifier;
import :support.invariant;
import std;

auto lookup_cpp_name(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID source_module,
    CppNameLookup lookup,
    std::span<const Span> components
) noexcept -> AnalysisResult<std::optional<CppNameReference>> {
    if (components.empty()) {
        invariant_violation("C++ lookup requires a name");
    }
    const auto root = draft.source_slice_copy(source_module, components.front());
    if (lookup == CppNameLookup::ModuleScope) {
        auto admitted = false;
        for (const auto& binding : catalog.cpp_imports(source_module)) {
            admitted |= binding.opens_namespace;
            if (!binding.opens_namespace && binding.components.back() == root) {
                admitted = true;
                usage.record_cpp(source_module, binding.origin);
            }
        }
        if (!admitted) {
            return std::nullopt;
        }
    }
    auto names = std::vector<std::string>();
    for (const auto component : components) {
        auto name = draft.source_slice_copy(source_module, component);
        if (!is_supported_cpp_identifier(name)) {
            return std::unexpected(draft.diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::CppIdentifier,
                    "external name cannot be represented as a C++ identifier"
                )
                    .primary(locate(draft.syntax_tree(source_module).view().source_id(), component))
                    .build()
            ));
        }
        names.push_back(std::move(name));
    }
    return CppNameReference {
        .context_module = catalog.find_module(source_module)->declaration,
        .lookup = lookup,
        .components = std::move(names)
    };
}
