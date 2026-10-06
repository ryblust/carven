module carven:frontend.program.parse;

import :diagnostics.diagnostic;
import :frontend.program;
import :source.batch;
import :source.manager;
import :support.timing;
import std;

auto parse_program(
    const SourceManager& sources,
    SourceBatch batch,
    TimingOutput timings = {}
) noexcept -> std::expected<SyntaxProgram, Diagnostics>;
