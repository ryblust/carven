module carven:semantic.analysis.async;

import :semantic.analysis.catalog;
import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.expr.scope;
import :semantic.analysis.model;
import :semantic.semir.async;
import :semantic.semir.program;
import std;

struct TimeoutBuiltin final {};

using AsyncBuiltin = std::variant<AsyncIntrinsic, AsyncCompositionPolicy, TimeoutBuiltin>;

auto find_async_builtin(std::string_view name) noexcept -> std::optional<AsyncBuiltin>;

auto resolve_async_builtin(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ConstructionRequests& requests,
    ProgramModuleID module,
    AsyncBuiltin builtin,
    Span span
) noexcept -> AnalysisTask<ResolvedStaticCallable>;

auto analyze_async_contracts(
    const AnalysisModelView& model,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<void>;

auto analyze_async_cancellation(const SemIRProgram& program) noexcept -> AsyncCancellationFacts;
