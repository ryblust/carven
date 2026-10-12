module carven:semantic.analysis.construction.impl;

import :semantic.analysis.body.context;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.analysis.construction.requests;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.semir.decl;
import :semantic.semir.type;
import :support.timing;
import std;

ProgramConstruction::ProgramConstruction(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage
) noexcept
    : draft(draft),
      catalog(catalog),
      requests(*this),
      static_stage(draft, requests),
      declarations(draft, catalog, usage, requests),
      bodies(draft, catalog, usage, requests) {}

auto ProgramConstruction::run() noexcept -> AnalysisResult<void> {
    return construct().run();
}

auto ProgramConstruction::construct() noexcept -> AnalysisTask<void> {
    auto declaration_scope = TimingScope(draft.timings(), TimingStage::SemanticDeclarations);
    auto result = (co_await declarations.run());
    if (!result) {
        co_return result;
    }
    static_cast<void>(analyze_nominal_containment(draft));
    static_cast<void>(diagnose_cpp_api_surface(draft, catalog));
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    declaration_scope.stop();
    if (auto* source = draft.source_analysis()) {
        const auto source_scope = TimingScope(draft.timings(), TimingStage::SourceObservations);
        for (const auto& symbol : catalog.symbols()) {
            const auto location = catalog.declaration_location(draft, symbol.symbol_id);
            source->declarations().record(draft, location, location, std::nullopt);
            const auto* structure = std::get_if<CatalogStructForm>(&symbol.form);
            if (structure == nullptr) {
                continue;
            }
            const auto declaration =
                draft.construction_struct_declaration_copy(structure->structure);
            for (const auto& [index, field] : std::views::enumerate(declaration.fields)) {
                const auto name = catalog.field_location(draft, structure->structure, index);
                source->declarations().record(draft, name, name, field.type);
            }
        }
        for (const auto& binding : catalog.imports()) {
            if (binding.selection_kind == CatalogImportSelectionKind::Wildcard) {
                continue;
            }
            for (const auto& selection : binding.selections) {
                const auto entity = catalog.source_entity(draft, selection.entity);
                const auto source_id = draft.syntax_tree(binding.importer).view().source_id();
                const auto alias = selection.aliased
                    ? std::optional(locate(source_id, selection.origin))
                    : std::nullopt;
                source->declarations().record(
                    draft,
                    locate(source_id, selection.export_origin),
                    entity,
                    std::nullopt
                );
                if (alias) {
                    source->declarations().record(draft, *alias, entity, std::nullopt, alias);
                }
            }
        }
        source->admit_declarations();
    }
    const auto body_scope = TimingScope(draft.timings(), TimingStage::SemanticBodies);
    result = (co_await bodies.run());
    if (!result) {
        co_return result;
    }
    static_cast<void>(analyze_nominal_containment(draft));
    if (const auto failure = draft.diagnostics().failure()) {
        if (auto* source = draft.source_analysis()) {
            source->discard();
        }
        co_return std::unexpected(*failure);
    }
    co_return {};
}

auto ProgramConstruction::construction_requests() noexcept -> ConstructionRequests& {
    return requests;
}
