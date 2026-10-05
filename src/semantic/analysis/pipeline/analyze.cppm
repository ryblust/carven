module carven:semantic.analyze;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

auto analyze(SyntaxProgram syntax, ExecutionOutput output = {}, TimingOutput timings = {}) noexcept
    -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;
