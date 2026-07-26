module carven:semantic.analysis.pipeline.declarations.impl;

import :semantic.analysis.analyzer;
import :semantic.analysis.catalog;
import :semantic.analysis.pipeline.declarations;
import :semantic.hir;
import :semantic.hir.stmt;
import :support.invariant;
import std;

auto collect_declarations(AnalysisCatalogView catalog, ProgramAnalyzer& draft) noexcept -> void {
    auto& builder = draft.builder();
    for (const auto& catalog_module : catalog.modules()) {
        const auto module_id = builder.append_module({
            .items = {},
        });
        if (module_id != catalog_module.module_id) {
            invariant_violation("catalog and HIR module identities are not aligned");
        }
    }

    for (const auto& symbol : catalog.symbols()) {
        const auto* enum_case = std::get_if<CatalogEnumCaseForm>(&symbol.form);
        const auto symbol_id = builder.append_symbol({
            .name = builder.intern_string(symbol.name),
            .module_id = symbol.module_id,
            .role = SemanticSymbolRole::Module,
            .parent = enum_case == nullptr ? std::nullopt
                                           : std::optional(catalog.enum_symbol(enum_case->owner)),
        });
        if (symbol_id != symbol.symbol_id) {
            invariant_violation("catalog and HIR symbol identities are not aligned");
        }
    }

    for (const auto& symbol : catalog.symbols()) {
        auto nominal = std::optional<HIRTypeValue>();
        if (const auto* structure = std::get_if<CatalogStructForm>(&symbol.form)) {
            nominal = HIRStructTypeValue {
                .structure = structure->structure,
            };
        } else if (const auto* enumeration = std::get_if<CatalogEnumForm>(&symbol.form)) {
            nominal = HIREnumTypeValue {
                .enumeration = enumeration->enumeration,
            };
        }
        if (nominal.has_value()) {
            builder.adopt_symbol_type(symbol.symbol_id, builder.intern_type({.value = *nominal}));
        }
    }
}
