module carven:driver.input;

import :driver.options;
import :driver.sources;
import :semantic.analysis.program.checked;
import :semantic.evaluation.native;
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
    const std::filesystem::path& crafts,
    const CompilationOptions& options,
    ExecutionOutput output,
    TimingOutput timings,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> std::expected<CompilationInput, std::string>;

// Consumes retained artifact bytes, leaving source and semantic owners intact.
auto encode_compilation_input(CompilationInput& input) noexcept
    -> std::expected<std::string, std::string>;
