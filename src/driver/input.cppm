module carven:driver.input;

import :driver.options;
import :driver.sources;
import :semantic.analysis.program.checked;
import :semantic.evaluation.output;
import :support.timing;
import std;

struct CompilationInput final {
    CommandSources sources;
    CheckedUnit semantic;
    // Restored units keep their bytes for publication without re-encoding.
    std::string artifact;
};

auto load_compilation_input(
    std::string_view executable,
    const CompilationOptions& options,
    ExecutionOutput output,
    TimingOutput timings
) noexcept -> std::optional<CompilationInput>;

// Consumes retained artifact bytes, leaving source and semantic owners intact.
auto encode_compilation_input(CompilationInput& input) noexcept
    -> std::expected<std::string, std::string>;
