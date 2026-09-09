module carven:semantic.analysis.validation;

import :semantic.analysis.diagnostics;
import :semantic.analysis.types.contents;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

auto verify_semantic_body(const SemIRBody& body, const SemIRProgram& program) noexcept -> void;

auto validate_global_semantic_contracts(
    const SemIRProgram& program,
    AnalysisDiagnostics diagnostics,
    std::span<const TypeContents> types
) noexcept -> AnalysisResult<void>;

auto validate_declaration_surfaces(
    const SemIRProgram& program,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<void>;
