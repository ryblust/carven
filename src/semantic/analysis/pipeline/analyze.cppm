module carven:semantic.analyze;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.evaluation.output;
import :semantic.semir.program;
import std;

auto analyze(SyntaxProgram syntax, const ConstantOutput& output = {}) noexcept
    -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;
