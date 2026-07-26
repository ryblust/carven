module carven:semantic.analysis.elaboration.module_analysis.impl;

import :diagnostics.builder;
import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.types;
import :support.invariant;
import std;

auto ModuleAnalysis::catalog() const noexcept -> AnalysisCatalogView {
    return catalog_view;
}

auto ModuleAnalysis::append_symbol(SemanticSymbolSpec spec) noexcept -> SymbolID {
    return hir_builder.append_symbol(std::move(spec));
}

auto ModuleAnalysis::builder() noexcept -> SemanticConstruction& {
    return hir_builder;
}

auto ModuleAnalysis::builder() const noexcept -> const SemanticConstruction& {
    return hir_builder;
}

auto ModuleAnalysis::declarations() const noexcept -> const DeclarationSessionView& {
    return declaration_session;
}

auto ModuleAnalysis::callable_constraints() noexcept -> CallableConstraints& {
    return deferred_callables;
}

auto ModuleAnalysis::entry_points() noexcept -> EntryPointTracker& {
    return entry_point_tracker;
}

auto ModuleAnalysis::tests() noexcept -> ModuleTestRegistry& {
    return test_registry;
}

auto ModuleAnalysis::module_id() const noexcept -> ProgramModuleID {
    return current_module_id;
}

auto ModuleAnalysis::source_id() const noexcept -> SourceID {
    return source().manager_source_id();
}

auto ModuleTestRegistry::register_name(std::string_view name, Span span) noexcept
    -> std::optional<Span> {
    const auto [position, inserted] = names.emplace(name, span);
    return inserted ? std::nullopt : std::optional(position->second);
}

auto ModuleAnalysis::syntax() const noexcept -> ASTView {
    return syntax_view;
}

auto ModuleAnalysis::resolve_declaration(SymbolID symbol_id, Span origin) noexcept -> bool {
    if (!hir_builder.symbol(symbol_id).module_id.has_value()) {
        return true;
    }
    const auto result = declaration_session.resolve(symbol_id, current_module_id, origin);
    if (!result) {
        observe_error();
    }
    return result;
}

auto ModuleAnalysis::error_checkpoint() const noexcept -> ErrorCheckpoint {
    return {
        .module_id = current_module_id,
        .failure_observations = error_observations,
    };
}

auto ModuleAnalysis::error_observed_since(ErrorCheckpoint checkpoint) const noexcept -> bool {
    if (checkpoint.module_id != current_module_id
        || checkpoint.failure_observations > error_observations) {
        invariant_violation("semantic failure checkpoint belongs to another operation");
    }
    return checkpoint.failure_observations != error_observations;
}

auto ModuleAnalysis::observe_error() noexcept -> void {
    if (error_observations == std::numeric_limits<std::size_t>::max()) {
        resource_limit_exceeded("semantic failure observations exhausted");
    }
    ++error_observations;
}

auto ModuleAnalysis::emit(Span span, std::string message, DiagnosticCode code) noexcept -> void {
    emit(DiagnosticBuilder(code, std::move(message)).primary(locate(source_id(), span)).build());
}

auto ModuleAnalysis::emit(Diagnostic diagnostic) noexcept -> void {
    if (diagnostic.finding.severity == DiagnosticSeverity::Error) {
        observe_error();
    }
    diagnostic_sink.emit(std::move(diagnostic));
}

auto ModuleAnalysis::recover_expression(Span span) noexcept -> HIRExprID {
    return append_expression(
        *this,
        {
            .origin = origin(span),
            .type = error_type(*this, span),
            .constant = std::nullopt,
            .value = HIRLiteralExpr {.value = HIRBooleanLiteralValue {false}},
        }
    );
}

auto ModuleAnalysis::diagnose_and_recover_expression(
    Span span,
    std::string message,
    DiagnosticCode code
) noexcept -> HIRExprID {
    emit(span, std::move(message), code);
    return recover_expression(span);
}

auto ModuleAnalysis::diagnose_and_recover_type(
    Span span,
    std::string message,
    DiagnosticCode code
) noexcept -> HIRTypeID {
    emit(span, std::move(message), code);
    return error_type(*this, span);
}

auto ModuleAnalysis::diagnostic_span(ProgramOriginID id) const noexcept -> SourceSpan {
    return builder().provenance().source_span(id);
}

auto ModuleAnalysis::origin(Span span) noexcept -> ProgramOriginID {
    return builder().append_origin({
        .source_id = builder().provenance().module_record(current_module_id).source_id,
        .span = span,
        .parent_origin_id = std::nullopt,
    });
}

auto ModuleAnalysis::spelling(Span span) const noexcept -> std::string_view {
    return source().slice(span);
}

auto ModuleAnalysis::source() const noexcept -> const ProgramSourceSnapshot& {
    return hir_builder.provenance().source_snapshot(
        hir_builder.provenance().module_record(current_module_id).source_id
    );
}

ModuleAnalysis::ModuleAnalysis(
    ProgramModuleID module_id,
    const SyntaxTree& syntax_tree,
    AnalysisCatalogView catalog,
    SemanticConstruction& builder,
    DeclarationSessionView declarations,
    CallableConstraints& constraints,
    EntryPointTracker& entry_points,
    DiagnosticSink& diagnostics
) noexcept
    : catalog_view(catalog),
      hir_builder(builder),
      declaration_session(declarations),
      deferred_callables(constraints),
      entry_point_tracker(entry_points),
      diagnostic_sink(diagnostics),
      current_module_id(module_id),
      syntax_view(syntax_tree.view()) {
    if (catalog.find_module(module_id) == nullptr) {
        invariant_violation("parsed module_analysis is absent from the analysis catalog");
    }
    if (syntax_view.source_id() != source_id()) {
        invariant_violation(
            "parsed module_analysis source does not match its module_analysis identity"
        );
    }
}
