module carven:semantic.analysis.pipeline.publish;

import :semantic.analysis.diagnostics;
import :semantic.semir.program;

auto publish_semantic_program(ProgramDraft&& draft) noexcept -> AnalysisResult<SemIRProgram>;
