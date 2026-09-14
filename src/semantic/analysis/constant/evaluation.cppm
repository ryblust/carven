module carven:semantic.analysis.constant.evaluation;

import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.evaluation.value;
import std;

auto evaluate_constant_root(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    const SemanticExpression& expression
) noexcept -> AnalysisResult<ConstantExecutionValue>;

auto evaluate_constant_test(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    BodyID body
) noexcept -> AnalysisResult<void>;
