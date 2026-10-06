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
    auto result = (co_await declarations.run());
    if (!result) {
        co_return result;
    }
    static_cast<void>(analyze_nominal_containment(draft));
    static_cast<void>(diagnose_cpp_api_surface(draft, catalog));
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    if (auto* source = draft.source_analysis()) {
        for (const auto& symbol : catalog.symbols()) {
            source->declare(
                catalog.declaration_location(draft, symbol.symbol_id),
                std::nullopt,
                std::nullopt
            );
            const auto* structure = std::get_if<CatalogStructForm>(&symbol.form);
            if (structure == nullptr) {
                continue;
            }
            const auto declaration =
                draft.construction_struct_declaration_copy(structure->structure);
            for (const auto& [index, field] : std::views::enumerate(declaration.fields)) {
                auto builtin = std::optional<BuiltinType>();
                if (const auto* concrete = std::get_if<TypeID>(&field.type)) {
                    const auto canonical = draft.type_copy(*concrete);
                    if (const auto* value = std::get_if<BuiltinTypeValue>(&canonical.value)) {
                        builtin = value->kind;
                    }
                }
                source->declare(
                    catalog.field_location(draft, structure->structure, index),
                    field.type,
                    builtin
                );
            }
        }
        source->begin_bodies();
    }
    co_return (co_await bodies.run());
}

auto ProgramConstruction::construction_requests() noexcept -> ConstructionRequests& {
    return requests;
}
