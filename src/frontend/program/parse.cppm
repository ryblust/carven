module carven:frontend.program.parse;

import :compiler.request;
import :diagnostics.diagnostic;
import :frontend.program;
import :source.manager;
import std;

auto parse_program(const SourceManager& sources, CompilationRequest request) noexcept
    -> std::expected<SyntaxProgram, Diagnostics>;
