module carven:driver.analysis.impl;

import :compiler.request;
import :diagnostics.report;
import :driver.analysis;
import :driver.input_path;
import :frontend.program.parse;
import :semantic.analyze;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

struct PreparedModuleInput final {
    std::string_view input_path;
    CanonicalModulePath module_path;
};

} // namespace

auto analyze_sources(
    std::span<const std::string_view> input_paths,
    const ExecutionOutput& output
) noexcept -> std::optional<SemIRProgram> {
    auto has_error = false;
    auto inputs = std::vector<PreparedModuleInput> {};
    inputs.reserve(input_paths.size());
    for (const auto input_path : input_paths) {
        auto module_path = derive_input_module_path(input_path);
        if (!module_path) {
            std::println(std::cerr, "carven: error: {}", module_path.error());
            has_error = true;
            continue;
        }
        inputs.push_back({
            .input_path = input_path,
            .module_path = std::move(*module_path),
        });
    }
    if (has_error) {
        return std::nullopt;
    }

    auto sources = SourceManager();
    auto module_inputs = std::vector<CompilationModuleInput> {};
    module_inputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        const auto source_id = sources.append_file(input.input_path);
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
            .module_path = input.module_path,
        });
    }
    if (has_error) {
        return std::nullopt;
    }

    auto syntax = parse_program(sources, CompilationRequest {.modules = module_inputs});
    if (!syntax) {
        std::print(std::cerr, "{}", render_diagnostics(syntax.error(), sources));
        return std::nullopt;
    }
    auto result = analyze(std::move(*syntax), output);
    if (!result) {
        std::print(std::cerr, "{}", render_diagnostics(result.error(), sources));
        return std::nullopt;
    }
    if (!result->diagnostics.empty()) {
        std::print(std::cerr, "{}", render_diagnostics(result->diagnostics, sources));
    }
    return std::move(result->value);
}
