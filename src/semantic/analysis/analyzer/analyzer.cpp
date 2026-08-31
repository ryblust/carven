module carven:semantic.analysis.analyzer.impl;

import :semantic.analysis.analyzer;
import :semantic.hir.place;
import :support.invariant;
import std;

ProgramAnalyzer::ProgramAnalyzer(ParsedBatch program) noexcept
    : ProgramAnalyzer(std::move(program).decompose()) {}

ProgramAnalyzer::ProgramAnalyzer(ParsedBatchParts parts) noexcept
    : syntax_by_module_id(std::move(parts.syntax_by_module)),
      semantic_session(std::move(parts.provenance)) {}

auto ProgramAnalyzer::builder() noexcept -> SemanticDraft& {
    return semantic_session.draft();
}

auto ProgramAnalyzer::builder() const noexcept -> SemanticDraftView {
    return semantic_session.draft();
}

auto ProgramAnalyzer::syntax(ProgramModuleID module_id) const noexcept -> const SyntaxTree& {
    return syntax_by_module_id.get(module_id);
}

auto ProgramAnalyzer::syntax_trees() const noexcept -> std::span<const SyntaxTree> {
    return syntax_by_module_id.values();
}

auto ProgramAnalyzer::module_count() const noexcept -> std::size_t {
    return syntax_by_module_id.size();
}

auto ProgramAnalyzer::callable_constraints() noexcept -> CallableConstraints& {
    return deferred_callables;
}

auto ProgramAnalyzer::callable_constraints() const noexcept -> const CallableConstraints& {
    return deferred_callables;
}

auto ProgramAnalyzer::entry_points() noexcept -> EntryPointTracker& {
    return entry_point_tracker;
}

auto ProgramAnalyzer::entry_points() const noexcept -> const EntryPointTracker& {
    return entry_point_tracker;
}

auto ProgramAnalyzer::diagnostics() noexcept -> DiagnosticSink& {
    return diagnostic_sink;
}

auto ProgramAnalyzer::has_errors() const noexcept -> bool {
    return diagnostic_sink.has_errors();
}

auto ProgramAnalyzer::take_diagnostics() noexcept -> Diagnostics {
    return diagnostic_sink.take();
}

auto ProgramAnalyzer::release_syntax() noexcept -> void {
    syntax_by_module_id = {};
}

auto CallableConstraints::append(DeferredCallableConstraint constraint) noexcept -> void {
    constraints.push_back(std::move(constraint));
}

auto CallableConstraints::values() const noexcept -> std::span<const DeferredCallableConstraint> {
    return constraints;
}

auto EntryPointTracker::origin() const noexcept -> std::optional<ProgramOriginID> {
    return entry_origin;
}

auto EntryPointTracker::record(ProgramOriginID value) noexcept -> void {
    entry_origin = value;
}

auto ProgramAnalyzer::finish() && noexcept -> SemanticProgram {
    return std::move(semantic_session).finish();
}

auto diagnostic_span(SemanticDraftView builder, ProgramOriginID id) noexcept -> SourceSpan {
    return builder.provenance().source_span(id);
}
