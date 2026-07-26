module carven:semantic.analyze.impl;

import :semantic.analysis.availability;
import :semantic.analysis.catalog;
import :semantic.analysis.control;
import :semantic.analysis.declaration_construction;
import :semantic.analysis.effects;
import :semantic.analysis.elaboration.declarations;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.lint;
import :semantic.analysis.pipeline.declarations;
import :semantic.analysis.storage_order;
import :semantic.analysis.validation;
import :semantic.analysis.validation.invariants;
import :semantic.analyze;
import :support.invariant;
import std;

auto analyze(ParsedBatch syntax) noexcept
    -> std::expected<Diagnosed<SemanticProgram>, Diagnostics> {
    auto analyzer = ProgramAnalyzer(std::move(syntax));
    {
        auto catalog_result =
            build_analysis_catalog(analyzer.builder().provenance(), analyzer.syntax_trees());
        if (!catalog_result.has_value()) {
            return std::unexpected(std::move(catalog_result.error()));
        }
        const auto catalog_owner = std::move(*catalog_result);
        const auto catalog = catalog_owner.view();
        collect_declarations(catalog, analyzer);
        auto declarations = DeclarationConstruction(catalog, analyzer.builder());

        auto modules = std::vector<ModuleAnalysis>();
        modules.reserve(analyzer.module_count());
        const auto declaration_view = declarations.view();
        for (auto index = 0uz; index < analyzer.module_count(); ++index) {
            const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
            modules.emplace_back(
                module_id,
                analyzer.syntax(module_id),
                catalog,
                analyzer.builder(),
                declaration_view,
                analyzer.callable_constraints(),
                analyzer.entry_points(),
                analyzer.diagnostics()
            );
        }
        const auto declaration_proofs = analyzer.builder().begin_expression_proof();
        auto resolved_declarations = std::move(declarations).resolve_all(modules);
        if (analyzer.has_errors()) {
            return std::unexpected(analyzer.take_diagnostics());
        }
        analyzer.builder().finish_expression_proof(declaration_proofs);
        modules.clear();
        const auto resolved_view = resolved_declarations.view();
        for (auto index = 0uz; index < analyzer.module_count(); ++index) {
            const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
            modules.emplace_back(
                module_id,
                analyzer.syntax(module_id),
                catalog,
                analyzer.builder(),
                resolved_view,
                analyzer.callable_constraints(),
                analyzer.entry_points(),
                analyzer.diagnostics()
            );
        }
        for (auto& module_analysis : modules) {
            build_module(module_analysis);
        }
        if (analyzer.has_errors()) {
            return std::unexpected(analyzer.take_diagnostics());
        }
        auto declaration_origins =
            std::vector<std::optional<ProgramOriginID>>(catalog.symbols().size());
        for (const auto& declaration : catalog.symbols()) {
            if (std::holds_alternative<CatalogConstantForm>(declaration.form)) {
                declaration_origins[declaration.symbol_id.index()] =
                    modules[declaration.module_id.index()].origin(declaration.declaration_span);
            }
        }
        modules.clear();
        std::move(resolved_declarations).finish();
        for (const auto& declaration : catalog.symbols()) {
            if (!std::holds_alternative<CatalogConstantForm>(declaration.form)) {
                continue;
            }
            const auto constant = analyzer.builder().symbol_constant(declaration.symbol_id);
            if (!constant.has_value()) {
                invariant_violation("resolved module constant is missing semantic facts");
            }
            diagnose_declaration_surface_constant(
                analyzer.builder(),
                analyzer.diagnostics(),
                declaration.visibility,
                declaration.module_id,
                *constant,
                *declaration_origins[declaration.symbol_id.index()],
                "module constant"
            );
        }
        if (analyzer.has_errors()) {
            return std::unexpected(analyzer.take_diagnostics());
        }
        diagnose_unused_bindings(analyzer.builder(), analyzer.diagnostics());
        diagnose_unused_imports(catalog, analyzer.builder().provenance(), analyzer.diagnostics());
    }
    analyzer.release_syntax();
    analyzer.builder().derive_places();
    if (const auto verified = verify_semantic_structure(analyzer.builder());
        !verified.has_value()) {
        invariant_violation(verified.error().message);
    }
    {
        auto control = solve_control(analyzer.builder());
        diagnose_effects(
            analyzer.builder(),
            analyzer.callable_constraints(),
            analyzer.diagnostics(),
            control
        );
        if (analyzer.has_errors()) {
            return std::unexpected(analyzer.take_diagnostics());
        }
        diagnose_availability(analyzer.builder(), analyzer.diagnostics(), control);
        if (analyzer.has_errors()) {
            return std::unexpected(analyzer.take_diagnostics());
        }
        commit_control_facts(analyzer.builder(), std::move(control));
    }
    diagnose_type_contracts(analyzer.builder(), analyzer.diagnostics());
    diagnose_nominal_storage(analyzer.builder(), analyzer.diagnostics());
    if (analyzer.has_errors()) {
        return std::unexpected(analyzer.take_diagnostics());
    }
    auto diagnostics = analyzer.take_diagnostics();
    return Diagnosed<SemanticProgram> {
        .value = std::move(analyzer).finish(),
        .diagnostics = std::move(diagnostics),
    };
}
