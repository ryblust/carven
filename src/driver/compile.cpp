module carven:driver.compile.impl;

import :artifacts;
import :artifacts.materialize;
import :backend.generate;
import :backend.generation.request;
import :compiler.manifest;
import :driver.analysis;
import :driver.compile;
import :driver.diagnostic;
import :driver.options;
import :driver.sources;
import :driver.timings;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.module_path;
import :support.file;
import :support.invariant;
import :support.path;
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

auto validate_manifest_destination(
    const std::filesystem::path& path,
    const CommandSources& sources,
    const ArtifactDestination& destination,
    const GeneratedArtifactSet& artifacts
) noexcept -> std::expected<void, std::string> {
    const auto resolve = [](const std::filesystem::path& candidate) static noexcept
        -> std::expected<std::filesystem::path, std::error_code> {
        auto error = std::error_code();
        const auto absolute = std::filesystem::absolute(candidate, error);
        if (error) {
            return std::unexpected(error);
        }
        const auto resolved = std::filesystem::weakly_canonical(absolute, error);
        if (error) {
            return std::unexpected(error);
        }
        return resolved;
    };
    const auto manifest = resolve(path);
    if (!manifest) {
        return std::unexpected(
            std::format(
                "cannot resolve artifact manifest '{}': {}",
                path.string(),
                manifest.error().message()
            )
        );
    }
    const auto collides = [&](const std::filesystem::path& candidate) noexcept {
        const auto resolved = resolve(candidate);
        if (resolved && *resolved == *manifest) {
            return true;
        }
        auto error = std::error_code();
        return std::filesystem::equivalent(path, candidate, error) && !error;
    };
    for (const auto& input : sources.carven) {
        if (collides(path_from_utf8(input.path))) {
            return std::unexpected("artifact manifest would overwrite a source input");
        }
    }
    for (const auto& input : sources.native) {
        if (collides(path_from_utf8(input))) {
            return std::unexpected("artifact manifest would overwrite a source input");
        }
    }
    if (const auto* directory = std::get_if<DirectoryArtifactDestination>(&destination)) {
        for (const auto& artifact : artifacts.entries()) {
            if (collides(directory->root / path_from_utf8(artifact.logical_path))) {
                return std::unexpected("artifact manifest would overwrite a generated artifact");
            }
        }
    }
    return {};
}

auto write_manifest(
    const std::filesystem::path& path,
    std::span<const SourceInput> sources,
    const GeneratedArtifactSet& artifacts
) noexcept -> std::expected<void, std::string> {
    auto error = std::error_code();
    const auto current = std::filesystem::current_path(error);
    if (error) {
        return std::unexpected(
            std::format("cannot resolve workspace directory: {}", error.message())
        );
    }
    const auto workspace_root = std::filesystem::canonical(current, error);
    if (error) {
        return std::unexpected(
            std::format("cannot resolve workspace directory: {}", error.message())
        );
    }
    auto paths = std::vector<std::string>();
    paths.reserve(sources.size());
    auto inputs = std::vector<CompilationManifestInput>();
    inputs.reserve(sources.size());
    for (const auto& source : sources) {
        const auto absolute =
            std::filesystem::absolute(path_from_utf8(source.path), error).lexically_normal();
        if (error) {
            return std::unexpected(
                std::format("cannot resolve manifest input '{}': {}", source.path, error.message())
            );
        }
        const auto relative = absolute.lexically_relative(workspace_root);
        const auto inside = !relative.empty() && *relative.begin() != "..";
        paths.push_back(path_to_generic_utf8(inside ? relative : absolute));
        inputs.push_back({.path = paths.back(), .module = source.module_path.value()});
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    if (error) {
        return std::unexpected(
            std::format(
                "cannot create artifact manifest directory '{}': {}",
                path.parent_path().string(),
                error.message()
            )
        );
    }
    if (const auto written = write_file(
            path,
            format_compilation_manifest(path_to_generic_utf8(workspace_root), inputs, artifacts)
        );
        !written) {
        return std::unexpected(
            std::format(
                "cannot write artifact manifest '{}': {}",
                path.string(),
                written.error().code.message()
            )
        );
    }
    return {};
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
            "      --artifact-manifest=<file>     Write JSON input and artifact facts\n"
            "      --linkage-domain=<id> Set the private namespace identity\n"
            "                            (default: derived from the output directory)\n"
            "      --library-domain=<id> Set the identity of a fixed source library\n"
            "      --library-module=<path> Select a canonical library module (repeatable)\n"
            "      --emit-library        Emit library implementations for their owning build\n"
            "                            (default: emit only consumer implementations)\n"
            "\n"
            "Test options:\n"
            "      --tests=main          Emit runtime tests, runner, and test entry\n"
            "      --tests=runner        Emit runtime tests and runner without test entry\n"
            "                            (default: no runtime test artifacts)\n"
            "\n"
            "Options:\n"
            "      --explicit-inputs     Use exactly the supplied source files\n"
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

    const auto sources = collect_command_sources(
        executable,
        request->input_paths,
        timings.output(),
        request->explicit_inputs
    );
    if (!sources) {
        return emit_driver_error(sources.error());
    }
    const auto write_output = [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
        const auto to_error = stream == ExecutionOutputStream::Error
            || std::holds_alternative<StandardOutputArtifactDestination>(request->destination);
        std::print(to_error ? std::cerr : std::cout, "{}", bytes);
    };
    auto semantic = load_and_analyze_sources(sources->carven, write_output, timings.output());
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
    const auto generation = TargetPlanningRequest {
        .test_mode = request->test_mode,
        .linkage_domain = std::move(*linkage_domain),
        .library = std::move(request->library),
    };
    const auto artifacts = generate_artifacts(*semantic, generation, selection, timings.output());
    if (!artifacts) {
        return emit_driver_error(artifacts.error());
    }
    if (request->artifact_manifest) {
        if (const auto valid = validate_manifest_destination(
                *request->artifact_manifest,
                *sources,
                request->destination,
                *artifacts
            );
            !valid) {
            return emit_driver_error(valid.error());
        }
    }

    auto writing = TimingScope(timings.output(), TimingStage::ArtifactWriting);
    const auto written = request->destination.visit(
        Overloaded {
            [&](const DirectoryArtifactDestination& destination) noexcept {
                return write_artifacts(destination.root, *artifacts);
            },
            [&](const StandardOutputArtifactDestination&) noexcept
                -> std::expected<void, std::string> {
                print_artifacts(*artifacts);
                return {};
            },
        }
    );
    if (!written) {
        return emit_driver_error(written.error());
    }
    if (request->artifact_manifest) {
        // Newly materialized paths can reveal filesystem aliases that did not
        // exist when the destination was first checked.
        if (const auto valid = validate_manifest_destination(
                *request->artifact_manifest,
                *sources,
                request->destination,
                *artifacts
            );
            !valid) {
            return emit_driver_error(valid.error());
        }
        if (const auto manifest =
                write_manifest(*request->artifact_manifest, sources->carven, *artifacts);
            !manifest) {
            return emit_driver_error(manifest.error());
        }
    }
    writing.stop();
    timings.set_outcome("finished");
    return 0;
}
