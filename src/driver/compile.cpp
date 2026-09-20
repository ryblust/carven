module carven:driver.compile.impl;

import :artifacts.materialize;
import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :driver.analysis;
import :driver.compile;
import :driver.diagnostic;
import :driver.options;
import :driver.sources;
import :driver.timings;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.module_path;
import :support.path;
import :support.invariant;
import :support.timing;
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

auto run_compile_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int {
    if (args.size() == 1
        && (std::string_view(args[0]) == "--help" || std::string_view(args[0]) == "-h")) {
        std::print(
            "Generate C++ headers and sources from application inputs and fixed Crafts roots.\n"
            "\n"
            "Usage:\n"
            "  carven compile [options] <source-file>...\n"
            "\n"
            "Output options:\n"
            "  -o, --output-dir <dir>    Write files below <dir> (default: .)\n"
            "      --stdout              Print explicit inputs with path headings\n"
            "      --linkage-domain=<id> Set the private namespace identity\n"
            "                            (default: derived from the output directory)\n"
            "\n"
            "Test options:\n"
            "      --tests               Emit runtime tests, runner, and test entry\n"
            "      --tests=default       Alias for --tests\n"
            "      --tests=external      Emit runtime tests and runner without test entry\n"
            "                            (default: no runtime test artifacts)\n"
            "\n"
            "Options:\n"
            "      --timings             Show total and stage timings on stderr\n"
            "  -h, --help                Show this help\n"
            "\n"
            "Sources include the fixed toolchain and working-directory Crafts roots.\n"
            "C++ compilation and linking belong to the consuming build.\n"
        );
        return 0;
    }
    auto request = parse_compile_command_options(args);
    if (!request) {
        return emit_driver_error(format_compile_option_error(request.error()), "carven compile");
    }

    auto timings = CommandTimings(request->timings, "compilation");
    auto linkage_domain = resolve_linkage_domain(*request);
    if (!linkage_domain.has_value()) {
        return emit_driver_error(linkage_domain.error());
    }

    const auto sources =
        collect_command_sources(executable, request->input_paths, timings.recorder());
    if (!sources) {
        return emit_driver_error(sources.error());
    }
    auto semantic = load_and_analyze_sources(
        sources->carven,
        [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            const auto to_error = stream == ExecutionOutputStream::Error
                || std::holds_alternative<StandardOutputArtifactDestination>(request->destination);
            std::print(to_error ? std::cerr : std::cout, "{}", bytes);
        },
        timings.recorder()
    );
    if (!semantic) {
        return 1;
    }
    auto displayed_modules = std::vector<CanonicalModulePath>();
    auto selection = std::optional<std::span<const CanonicalModulePath>>();
    if (std::holds_alternative<StandardOutputArtifactDestination>(request->destination)) {
        for (const auto& source : sources->carven) {
            for (const auto input : request->input_paths) {
                auto error = std::error_code();
                if (std::filesystem::equivalent(
                        path_from_utf8(source.path),
                        path_from_utf8(input),
                        error
                    )) {
                    displayed_modules.push_back(source.module_path);
                    break;
                }
            }
        }
        selection = displayed_modules;
    }
    auto generation = TimingScope(timings.recorder(), TimingStage::CppGeneration);
    const auto artifacts = generate_artifacts(
        std::move(*semantic),
        TargetPlanningRequest {
            .test_mode = request->test_mode,
            .linkage_domain = std::move(*linkage_domain),
        },
        selection
    );

    generation.stop();
    auto writing = TimingScope(timings.recorder(), TimingStage::ArtifactWriting);
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
    writing.stop();
    if (!written) {
        return emit_driver_error(written.error());
    }
    timings.set_outcome("finished");
    return 0;
}
