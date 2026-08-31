module carven:semantic.analysis.elaboration.module_analysis;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.ast.ids;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.analyzer;
import :semantic.analysis.session;
import :semantic.analysis.session.read;
import :semantic.analysis.catalog;
import :semantic.analysis.decl;
import :semantic.hir.ids;
import :source.provenance;
import :source.text;
import std;

struct ErrorCheckpoint final {
    ProgramModuleID module_id;
    std::size_t failure_observations;
};

class ModuleTestRegistry final {
public:
    auto register_name(std::string_view name, Span span) noexcept -> std::optional<Span>;

private:
    std::flat_map<std::string, Span, std::less<>> names;
};

class ModuleAnalysis final {
public:
    ModuleAnalysis(
        ProgramModuleID module_id,
        const SyntaxTree& syntax_tree,
        AnalysisCatalogView catalog,
        SemanticDraft& builder,
        DeclarationContractView declarations,
        CallableConstraints& constraints,
        EntryPointTracker& entry_points,
        DiagnosticSink& diagnostics
    ) noexcept;

    auto catalog() const noexcept -> AnalysisCatalogView;
    auto append_symbol(SemanticSymbolSpec spec) noexcept -> SymbolID;
    auto builder() noexcept -> SemanticDraft&;
    auto builder() const noexcept -> SemanticDraftView;
    auto declarations() const noexcept -> const DeclarationContractView&;
    auto callable_constraints() noexcept -> CallableConstraints&;
    auto entry_points() noexcept -> EntryPointTracker&;
    auto tests() noexcept -> ModuleTestRegistry&;
    auto module_id() const noexcept -> ProgramModuleID;
    auto source_id() const noexcept -> SourceID;
    auto syntax() const noexcept -> ASTView;
    auto resolve_declaration(SymbolID symbol_id, Span origin) noexcept -> bool;
    auto error_checkpoint() const noexcept -> ErrorCheckpoint;
    auto error_observed_since(ErrorCheckpoint checkpoint) const noexcept -> bool;

    auto spelling(Span span) const noexcept -> std::string_view;

    auto origin(Span span) noexcept -> ProgramOriginID;

    auto diagnostic_span(ProgramOriginID id) const noexcept -> SourceSpan;

    auto emit(Span span, std::string message, DiagnosticCode code) noexcept -> void;
    auto emit(Diagnostic diagnostic) noexcept -> void;

    auto recover_expression(Span span) noexcept -> HIRExprID;

    auto diagnose_and_recover_expression(
        Span span,
        std::string message,
        DiagnosticCode code
    ) noexcept -> HIRExprID;

    auto diagnose_and_recover_type(Span span, std::string message, DiagnosticCode code) noexcept
        -> HIRTypeID;

private:
    auto source() const noexcept -> const ProgramSourceSnapshot&;
    auto observe_error() noexcept -> void;

    AnalysisCatalogView catalog_view;
    SemanticDraft& hir_builder;
    DeclarationContractView declaration_contracts;
    CallableConstraints& deferred_callables;
    EntryPointTracker& entry_point_tracker;
    DiagnosticSink& diagnostic_sink;
    ModuleTestRegistry test_registry;
    ProgramModuleID current_module_id;
    ASTView syntax_view;
    std::size_t error_observations = 0;
};
