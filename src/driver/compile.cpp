module carven:driver.compile.impl;

import :artifacts.materialize;
import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :driver.analysis;
import :driver.compile;
import :driver.options;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto resolve_linkage_domain(CompileCommandOptions& options) noexcept
    -> std::expected<LinkageDomain, std::string> {
    if (options.linkage_domain.has_value()) {
        return std::move(*options.linkage_domain);
    }
    const auto root = options.destination.visit(
        Overloaded {
            [](const DirectoryArtifactDestination& destination) static noexcept {
                return destination.root;
            },
            [](const StandardOutputArtifactDestination&) static noexcept {
                return std::filesystem::path(".");
            },
        }
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

auto print_artifacts(const GeneratedArtifactSet& artifacts) noexcept -> void {
    for (const auto& artifact : artifacts.entries()) {
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

    auto semantic = load_and_analyze_sources(
        request->input_paths,
        [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            const auto to_error = stream == ExecutionOutputStream::Error
                || std::holds_alternative<StandardOutputArtifactDestination>(request->destination);
            std::print(to_error ? std::cerr : std::cout, "{}", bytes);
        }
    );
    if (!semantic) {
        return 1;
    }
    const auto artifacts = generate_artifacts(
        std::move(*semantic),
        TargetPlanningRequest {
            .test_mode = request->test_mode,
            .linkage_domain = std::move(*linkage_domain),
        }
    );

    const auto written = request->destination.visit(
        Overloaded {
            [&](const DirectoryArtifactDestination& destination) noexcept {
                return write_artifacts(destination.root, artifacts);
            },
            [&](const StandardOutputArtifactDestination&) noexcept
                -> std::expected<void, std::string> {
                print_artifacts(artifacts);
                return {};
            },
        }
    );
    if (!written) {
        std::println(std::cerr, "carven: error: {}", written.error());
        return 1;
    }
    return 0;
}
