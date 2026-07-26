module carven:semantic.analyze;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.hir;
import std;

auto analyze(ParsedBatch syntax) noexcept -> std::expected<Diagnosed<SemanticProgram>, Diagnostics>;
