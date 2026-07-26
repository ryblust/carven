module carven:semantic.analysis.analyzer;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program;
import :semantic.analysis.builder;
import :semantic.analysis.catalog;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :source.text;
import :support.id_table;
import std;

struct DeferredCallableConstraint final {
    HIRExprID source;
    std::variant<HIRTypeID, HIRExprID> target;
    ProgramOriginID origin;
    DiagnosticCode code;
    std::string message;
};

class CallableConstraints final {
public:
    auto append(DeferredCallableConstraint constraint) noexcept -> void;
    auto values() const noexcept -> std::span<const DeferredCallableConstraint>;

private:
    std::vector<DeferredCallableConstraint> constraints;
};

class EntryPointTracker final {
public:
    auto origin() const noexcept -> std::optional<ProgramOriginID>;
    auto record(ProgramOriginID value) noexcept -> void;

private:
    std::optional<ProgramOriginID> entry_origin;
};

class ProgramAnalyzer final {
public:
    explicit ProgramAnalyzer(ParsedBatch program) noexcept;

    auto builder() noexcept -> SemanticConstruction&;
    auto builder() const noexcept -> const SemanticConstruction&;
    auto syntax(ProgramModuleID module_id) const noexcept -> const SyntaxTree&;
    auto syntax_trees() const noexcept -> std::span<const SyntaxTree>;
    auto module_count() const noexcept -> std::size_t;
    auto callable_constraints() noexcept -> CallableConstraints&;
    auto callable_constraints() const noexcept -> const CallableConstraints&;
    auto entry_points() noexcept -> EntryPointTracker&;
    auto entry_points() const noexcept -> const EntryPointTracker&;
    auto diagnostics() noexcept -> DiagnosticSink&;
    auto has_errors() const noexcept -> bool;
    auto take_diagnostics() noexcept -> Diagnostics;
    auto release_syntax() noexcept -> void;

    auto finish() && noexcept -> SemanticProgram;

private:
    explicit ProgramAnalyzer(ParsedBatchParts parts) noexcept;

    IDTable<SyntaxTree, ProgramModuleID> syntax_by_module_id;
    SemanticConstruction hir_builder;
    CallableConstraints deferred_callables;
    EntryPointTracker entry_point_tracker;
    DiagnosticSink diagnostic_sink;
};

auto diagnostic_span(const SemanticConstruction& builder, ProgramOriginID id) noexcept
    -> SourceSpan;
