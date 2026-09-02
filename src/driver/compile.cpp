module carven:driver.compile.impl;

import :artifacts;
import :artifacts.materialize;
import :backend.generation.request;
import :compiler.compile;
import :compilation.request;
import :diagnostics.report;
import :driver.compile;
import :driver.input_path;
import :driver.options;
import :source.manager;
import :source.module_path;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

namespace {

struct PreparedModuleInput final {
    std::string_view input_path;
    CanonicalModulePath module_path;
};

auto resolve_linkage_domain(CompileCommandOptions& options) noexcept
    -> std::expected<LinkageDomain, std::string> {
    if (options.linkage_domain.has_value()) {
        return std::move(*options.linkage_domain);
    }
    const auto root = std::visit(
        Overloaded {
            [](const DirectoryArtifactDestination& destination) static noexcept {
                return destination.root;
            },
            [](const StandardOutputArtifactDestination&) static noexcept {
                return std::filesystem::path(".");
            },
        },
        options.destination
    );
    auto error = std::error_code();
    const auto absolute = std::filesystem::absolute(root, error);
    if (error) {
        return std::unexpected(
            std::format(
                "cannot resolve linkage domain from output root '{}': {}",
                root.string(),
                error.message()
            )
        );
    }
    auto domain = LinkageDomain::artifact_root(absolute);
    if (!domain.has_value()) {
        invariant_violation("absolute artifact root did not form a linkage domain");
    }
    return std::move(*domain);
}

auto print_artifacts(const ArtifactSet& artifacts) noexcept -> void {
    for (const auto& artifact : artifacts.artifacts()) {
        std::println("==> {} <==", artifact.logical_path);
        std::print("{}", artifact.content);
        if (!artifact.content.ends_with('\n')) {
            std::println();
        }
    }
}

} // namespace

auto run_compile_command(std::span<const char* const> args) noexcept -> int {
    auto request = parse_compile_command_options(args);
    if (!request) {
        std::println(std::cerr, "carven: error: {}", format_compile_option_error(request.error()));
        return 1;
    }

    auto linkage_domain = resolve_linkage_domain(*request);
    if (!linkage_domain.has_value()) {
        std::println(std::cerr, "carven: error: {}", linkage_domain.error());
        return 1;
    }

    auto exit_code = 0;
    auto inputs = std::vector<PreparedModuleInput> {};
    inputs.reserve(request->input_paths.size());
    for (const auto input_path : request->input_paths) {
        auto module_path = derive_input_module_path(input_path);
        if (!module_path) {
            std::println(std::cerr, "carven: error: {}", module_path.error());
            exit_code = 1;
            continue;
        }
        inputs.push_back({
            .input_path = input_path,
            .module_path = std::move(*module_path),
        });
    }
    if (exit_code != 0) {
        return exit_code;
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
            exit_code = 1;
            continue;
        }
        module_inputs.push_back({
            .source_id = *source_id,
            .module_path = input.module_path,
        });
    }
    if (exit_code != 0) {
        return exit_code;
    }

    const auto result = compile(
        sources,
        CompilationRequest {.modules = module_inputs},
        TargetGenerationRequest {
            .test_mode = request->test_mode,
            .linkage_domain = std::move(*linkage_domain),
        }
    );
    if (!result) {
        std::print(std::cerr, "{}", render_diagnostics(result.error(), sources));
        return 1;
    }
    if (!result->diagnostics.empty()) {
        std::print(std::cerr, "{}", render_diagnostics(result->diagnostics, sources));
    }

    const auto written = std::visit(
        Overloaded {
            [&](const DirectoryArtifactDestination& destination) noexcept {
                return write_artifacts(destination.root, result->value);
            },
            [&](const StandardOutputArtifactDestination&) noexcept
                -> std::expected<void, std::string> {
                print_artifacts(result->value);
                return {};
            },
        },
        request->destination
    );
    if (!written) {
        std::println(std::cerr, "carven: error: {}", written.error());
        return 1;
    }
    return 0;
}
