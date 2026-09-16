module carven:driver.analysis.impl;

import :compiler.analysis;
import :compiler.request;
import :diagnostics.report;
import :driver.analysis;
import :driver.input_path;
import :source.manager;
import :source.text;
import std;

auto load_and_analyze_sources(
    std::span<const std::string_view> input_paths,
    const ExecutionOutput& output
) noexcept -> std::optional<SemIRProgram> {
    auto has_error = false;
    auto sources = SourceManager();
    auto module_inputs = std::vector<CompilationModuleInput> {};
    module_inputs.reserve(input_paths.size());
    for (const auto input_path : input_paths) {
        auto module_path = derive_input_module_path(input_path);
        if (!module_path) {
            std::println(std::cerr, "carven: error: {}", module_path.error());
            has_error = true;
            continue;
        }
        const auto source_id = sources.append_file(input_path);
        if (!source_id) {
            std::println(
                std::cerr,
                "carven: error: {}: '{}'",
                source_id.error().message,
                source_id.error().origin
            );
            has_error = true;
            continue;
        }
        module_inputs.push_back({
            .source_id = *source_id,
            .module_path = std::move(*module_path),
        });
    }
    if (has_error) {
        return std::nullopt;
    }

    auto result =
        analyze_compilation(sources, CompilationRequest {.modules = module_inputs}, output);
    if (!result) {
        std::print(std::cerr, "{}", render_diagnostics(result.error(), sources));
        return std::nullopt;
    }
    if (!result->diagnostics.empty()) {
        std::print(std::cerr, "{}", render_diagnostics(result->diagnostics, sources));
    }
    return std::move(result->value);
}
