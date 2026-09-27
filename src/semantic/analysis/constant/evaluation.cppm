module carven:semantic.analysis.constant.evaluation;

import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.evaluation.value;
import :semantic.semir.decl;
import std;

auto evaluate_constant_root(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    const SemanticExpression& expression
) noexcept -> AnalysisTask<ExecutionValue>;

struct ConstantBodyRoot final {
    BodyID body;
    BlockSource source;
};

auto evaluate_constant_body(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    ConstantBodyRoot root
) noexcept -> AnalysisTask<void>;
