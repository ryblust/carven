module carven:semantic.analysis.interop;

import :frontend.ast.decl;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.program;
import std;

auto validate_cpp_provider(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    const ASTFunctionDecl& function
) noexcept -> AnalysisResult<void>;

auto diagnose_cpp_api_surface(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void>;
