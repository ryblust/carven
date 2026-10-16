module carven:driver.compile.impl;

import :artifacts;
import :artifacts.materialize;
import :backend.generate;
import :backend.generation.request;
import :compiler.manifest;
import :driver.compile;
import :driver.diagnostic;
import :driver.input;
import :driver.options;
import :driver.sources;
import :driver.timings;
import :semantic.evaluation.output;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.table;
import :source.module_path;
import :support.file;
import :support.invariant;
import :support.path;
import :support.timing;
import :support.visit;
import std;

namespace {

auto resolve_linkage_domain(const ArtifactDestination& destination) noexcept
    -> std::expected<LinkageDomain, std::string> {
    const auto root = destination.visit(
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

auto write_manifest(
    const std::filesystem::path& path,
    const CommandSources& sources,
    const GeneratedArtifactSet& artifacts,
    std::size_t runtime_test_count,
    bool cacheable
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
    paths.reserve(sources.carven.size());
    auto inputs = std::vector<CompilationManifestInput>();
    inputs.reserve(sources.carven.size());
    for (const auto& source : sources.carven) {
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
    auto owned_modules = std::vector<std::string_view>();
    for (const auto& module : sources.owned_modules) {
        owned_modules.push_back(module.value());
    }
    auto standard_modules = std::vector<std::string_view>();
    for (const auto& module : sources.standard_modules) {
        standard_modules.push_back(module.value());
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
            format_compilation_manifest(
                path_to_generic_utf8(workspace_root),
                inputs,
                artifacts,
                runtime_test_count,
                cacheable,
                owned_modules,
                standard_modules
            )
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

auto run_compilation_command(
    std::string_view executable,
    std::span<const char* const> args,
    CompilationCommand command
) noexcept -> int {
    if (args.size() == 1
        && (std::string_view(args[0]) == "--help" || std::string_view(args[0]) == "-h")) {
        if (command == CompilationCommand::Check) {
            std::print(
                "Check sources, evaluate compile-time code, and run const tests.\n"
                "\n"
                "Usage:\n"
                "  carven check [options] [<input>...]\n"
                "\n"
                "Options:\n"
                "  -o <dir>                        Retain semantics and a manifest below <dir>\n"
                "      --source=<module>=<path>    Map an owned source module (repeatable)\n"
                "      --timings                   Show total and stage timings on stderr\n"
                "  -h, --help                      Show this help\n"
                "\n"
                "Without -o, checking writes no artifacts. C++ is not generated.\n"
            );
        } else {
            std::print(
                "Generate C++ artifacts from sources or retained semantics.\n"
                "\n"
                "Usage:\n"
                "  carven compile [options] [<input>...]\n"
                "\n"
                "Output options:\n"
                "  -o <dir>                        Write artifacts below <dir> (default: .)\n"
                "      --stdout                    Print selected C++ with path headings\n"
                "\n"
                "Test options:\n"
                "      --tests                     Emit runtime tests, runner, and a test entry\n"
                "      --tests=runner              Emit tests and runner; keep the program entry\n"
                "                                  (default: no runtime test artifacts)\n"
                "\n"
                "Input options:\n"
                "      --source=<module>=<path>    Map an owned source module (repeatable)\n"
                "\n"
                "Options:\n"
                "      --timings                   Show total and stage timings on stderr\n"
                "  -h, --help                      Show this help\n"
            );
        }
        std::print(
            "\n"
            "Installed Crafts supply source dependencies. Source inputs also collect\n"
            "project Crafts; mapped inputs select the complete project source set.\n"
            "A standard-library artifact directory can accompany new sources.\n"
            "Use 'check -o <dir>' to retain semantics and 'compile' to generate C++.\n"
            "The consuming build system compiles and links the generated C++.\n"
        );
        return 0;
    }
    auto request = parse_compilation_options(args, command);
    if (!request) {
        return emit_driver_error(
            format_compilation_option_error(request.error()),
            command == CompilationCommand::Check ? "carven check" : "carven compile"
        );
    }

    auto timings = CommandTimings(
        request->timings,
        command == CompilationCommand::Check ? "check" : "compilation"
    );
    const auto finish = [&]() noexcept {
        timings.set_outcome(command == CompilationCommand::Check ? "passed" : "finished");
        if (command == CompilationCommand::Check && !request->timings) {
            std::cout.flush();
            std::println(std::cerr, "carven: check passed");
        }
        return 0;
    };
    const auto printing_artifacts = request->destination
        && std::holds_alternative<StandardOutputArtifactDestination>(*request->destination);
    const auto write_output = [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
        const auto to_error = stream == ExecutionOutputStream::Error || printing_artifacts;
        std::print(to_error ? std::cerr : std::cout, "{}", bytes);
    };
    auto input = load_compilation_input(executable, *request, write_output, timings.output());
    if (!input) {
        return 1;
    }
    if (!request->destination) {
        return finish();
    }
    const auto& destination = *request->destination;
    const auto manifest_path = destination.visit(
        Overloaded {
            [](const DirectoryArtifactDestination& destination) static noexcept
                -> std::optional<std::filesystem::path> {
                return destination.root / "carven/manifest.json";
            },
            [](const StandardOutputArtifactDestination&) static noexcept
                -> std::optional<std::filesystem::path> { return std::nullopt; },
        }
    );
    const auto* sources = &input->sources;
    const auto* semantic = &input->semantic.program;
    auto displayed_modules = std::vector<CanonicalModulePath>();
    auto selection = std::optional<std::span<const CanonicalModulePath>>();
    if (printing_artifacts) {
        for (const auto& source : sources->carven) {
            if (std::ranges::any_of(request->mapped_inputs, [&](const SourceInput& input) noexcept {
                    return input.module_path == source.module_path;
                })) {
                displayed_modules.push_back(source.module_path);
                continue;
            }
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
        if (displayed_modules.empty() && request->mapped_inputs.empty()) {
            displayed_modules = sources->owned_modules;
        }
        selection = displayed_modules;
    }
    const auto runtime_test_count = static_cast<std::size_t>(
        std::ranges::count_if(semantic->tests().entries(), [&](const auto& record) noexcept {
            const auto& test = record.value;
            const auto& module = semantic->declarations().module_decl(test.module_id);
            const auto& path = semantic->provenance().module_record(module.provenance_module).path;
            return !test.is_const && std::ranges::contains(sources->owned_modules, path);
        })
    );
    const auto artifacts = [&]() noexcept -> std::expected<GeneratedArtifactSet, std::string> {
        if (command == CompilationCommand::Compile) {
            auto linkage_domain = resolve_linkage_domain(destination);
            if (!linkage_domain) {
                return std::unexpected(std::move(linkage_domain.error()));
            }
            const auto generation = TargetPlanningRequest {
                .test_mode = request->test_mode,
                .linkage_domain = std::move(*linkage_domain),
                .library = standard_source_library(*sources),
            };
            return generate_artifacts(*semantic, generation, selection, timings.output());
        }
        auto bytes = encode_compilation_input(*input);
        if (!bytes) {
            return std::unexpected(std::move(bytes.error()));
        }
        auto entries = std::vector<GeneratedArtifact>();
        entries.push_back({
            .logical_path = "carven/semantic",
            .role = GeneratedArtifactRole::SemanticIR,
            .source_mapping = ArtifactSourceMappingPolicy::StableInterface,
            .source_path = std::nullopt,
            .content = std::move(*bytes),
        });
        return GeneratedArtifactSet(std::move(entries));
    }();
    if (!artifacts) {
        return emit_driver_error(artifacts.error());
    }

    auto writing = TimingScope(timings.output(), TimingStage::ArtifactWriting);
    const auto written = destination.visit(
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
    if (manifest_path) {
        if (const auto manifest = write_manifest(
                *manifest_path,
                *sources,
                *artifacts,
                runtime_test_count,
                input->semantic.cacheable
            );
            !manifest) {
            return emit_driver_error(manifest.error());
        }
    }
    writing.stop();
    return finish();
}
