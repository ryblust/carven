module carven:frontend.program.parse;

import :compilation.request;
import :diagnostics.diagnostic;
import :frontend.program;
import :source.manager;
import std;

auto parse(const SourceManager& sources, std::span<const CompilationInput> inputs) noexcept
    -> std::expected<ParsedBatch, Diagnostics>;
