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
import :support.process;
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

// Query the same ordinary build entry that a user can run in the Craft.
// The child owns its working directory and arguments; no process-global state changes.
auto query_craft(const std::filesystem::path& root) noexcept
    -> std::expected<std::string, std::string> {
    const auto executable = process::executable();
    if (!executable) {
        return std::unexpected("cannot locate the running compiler for a Craft query");
    }
    const auto arguments = std::array {std::string("query")};
    auto result = process::run(*executable, arguments, root, process::Stdio::CaptureOutput);
    if (!result) {
        return std::unexpected(
            std::format("cannot query Craft '{}': {}", root.string(), result.error().message())
        );
    }
    if (result->status != 0) {
        return std::unexpected(std::format("Craft query failed for '{}'", root.string()));
    }
    return std::move(result->output);
}

auto collect_manifest_resources(
    const std::filesystem::path& crafts,
    const CommandSources& sources,
    bool query,
    std::span<const std::string> native_inputs
) noexcept -> std::expected<CompilationManifestResources, std::string> {
    auto error = std::error_code();
    const auto root = std::filesystem::canonical(crafts, error);
    if (error) {
        return std::unexpected(std::format("cannot resolve Carven Crafts: {}", error.message()));
    }
    auto craft_roots = std::set<std::filesystem::path> {root / "carven"};
    for (const auto& source : sources.carven) {
        if (!source.module_path.module_domain_prefix().craft_name()
            || std::ranges::contains(sources.standard_modules, source.module_path)) {
            continue;
        }
        auto physical = std::filesystem::weakly_canonical(path_from_utf8(source.path), error);
        if (error) {
            return std::unexpected(
                std::format("cannot resolve Craft source '{}': {}", source.path, error.message())
            );
        }
        for (auto remaining = source.module_path.domain_relative_components().size();
             remaining != 0;
             --remaining) {
            physical = physical.parent_path();
        }
        craft_roots.insert(std::move(physical));
    }
    const auto project_root =
        sources.project_root.empty() ? std::filesystem::current_path(error) : sources.project_root;
    if (error) {
        return std::unexpected(
            std::format("cannot resolve project directory: {}", error.message())
        );
    }
    const auto project_crafts = std::filesystem::weakly_canonical(project_root / "crafts", error);
    if (error) {
        return std::unexpected(std::format("cannot resolve project Crafts: {}", error.message()));
    }
    for (const auto& source : native_inputs) {
        const auto relative = path_from_utf8(source).lexically_relative(project_crafts);
        if (!relative.empty()
            && *relative.begin() != ".."
            && *relative.begin() != "."
            && std::next(relative.begin()) != relative.end()) {
            craft_roots.insert(project_crafts / *relative.begin());
        }
    }
    auto include_dirs = std::set<std::string>();
    auto resources = CompilationManifestResources {
        .include_dirs = {},
        .library_roots = {},
        .craft_roots = {},
        .crafts = {}
    };
    // These are discovery inputs, not compilation roots. The consumer watches
    // file names here and contents only for the actual manifest inputs.
    if (!sources.library_roots.empty()) {
        for (const auto& directory : sources.library_roots) {
            resources.library_roots.push_back(path_to_generic_utf8(directory));
        }
    } else {
        resources.library_roots.push_back(path_to_generic_utf8(root / "carven"));
        resources.library_roots.push_back(
            path_to_generic_utf8(std::filesystem::absolute("crafts", error).lexically_normal())
        );
        if (error) {
            return std::unexpected(
                std::format("cannot resolve project Crafts: {}", error.message())
            );
        }
    }
    for (const auto& craft_root : craft_roots) {
        include_dirs.insert(path_to_generic_utf8(craft_root.parent_path()));
        resources.craft_roots.push_back(path_to_generic_utf8(craft_root));
        if (!query) {
            continue;
        }
        const auto declaration = craft_root / "build.cv";
        const auto status = std::filesystem::status(declaration, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            continue;
        }
        if (error) {
            return std::unexpected(
                std::format(
                    "cannot inspect Craft build '{}': {}",
                    path_to_generic_utf8(declaration),
                    error.message()
                )
            );
        }
        if (!std::filesystem::exists(status)) {
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            return std::unexpected(
                std::format(
                    "Craft build '{}' is not a regular file",
                    path_to_generic_utf8(declaration)
                )
            );
        }
        auto description = query_craft(craft_root);
        if (!description) {
            return std::unexpected(std::move(description.error()));
        }
        resources.crafts.push_back(std::move(*description));
    }
    resources.include_dirs.assign(include_dirs.begin(), include_dirs.end());
    return resources;
}

auto write_manifest(
    const std::filesystem::path& path,
    const std::filesystem::path& crafts,
    const CommandSources& sources,
    const GeneratedArtifactSet& artifacts,
    std::size_t runtime_test_count,
    bool cacheable,
    bool query,
    std::span<const std::string> native_inputs
) noexcept -> std::expected<void, std::string> {
    const auto resources = collect_manifest_resources(crafts, sources, query, native_inputs);
    if (!resources) {
        return std::unexpected(resources.error());
    }
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
    paths.reserve(sources.carven.size() + native_inputs.size());
    auto inputs = std::vector<CompilationManifestInput>();
    inputs.reserve(sources.carven.size() + native_inputs.size());
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
        inputs.push_back({.path = paths.back(), .module_path = source.module_path.value()});
    }
    for (const auto& path : native_inputs) {
        const auto absolute = path_from_utf8(path);
        const auto relative = absolute.lexically_relative(workspace_root);
        const auto inside = !relative.empty() && *relative.begin() != "..";
        paths.push_back(path_to_generic_utf8(inside ? relative : absolute));
        inputs.push_back({.path = paths.back(), .module_path = {}});
    }
    auto owned_modules = std::vector<std::string_view>();
    for (const auto& module_path : sources.owned_modules) {
        owned_modules.push_back(module_path.value());
    }
    auto standard_modules = std::vector<std::string_view>();
    for (const auto& module_path : sources.standard_modules) {
        standard_modules.push_back(module_path.value());
    }
    auto external_modules = std::vector<std::string_view>();
    for (const auto& module_path : sources.external_modules) {
        external_modules.push_back(module_path.value());
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
                *resources,
                inputs,
                artifacts,
                runtime_test_count,
                cacheable,
                owned_modules,
                standard_modules,
                external_modules
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

struct CompiledArtifacts final {
    CommandSources sources;
    GeneratedArtifactSet artifacts;
    std::size_t runtime_test_count;
    bool cacheable;
};

auto compile_artifacts(
    const std::filesystem::path& crafts,
    const CompilationOptions& request,
    ExecutionOutput output,
    TimingOutput timings,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> std::expected<CompiledArtifacts, std::string> {
    if (request.input_paths.empty()) {
        return CompiledArtifacts {
            .sources =
                {
                    .project_root = {},
                    .crafts = crafts,
                    .library_roots = {},
                    .carven = {},
                    .standard_modules = {},
                    .owned_modules = {},
                    .external_modules = {},
                },
            .artifacts = GeneratedArtifactSet({}),
            .runtime_test_count = 0,
            .cacheable = true,
        };
    }
    auto input = load_compilation_input(crafts, request, output, timings, native_bindings);
    if (!input) {
        return std::unexpected(std::move(input.error()));
    }
    if (!request.destination) {
        return CompiledArtifacts {
            .sources = std::move(input->sources),
            .artifacts = GeneratedArtifactSet(std::vector<GeneratedArtifact>()),
            .runtime_test_count = 0,
            .cacheable = input->semantic.cacheable,
        };
    }
    const auto& destination = *request.destination;
    const auto printing_artifacts =
        std::holds_alternative<StandardOutputArtifactDestination>(destination);
    const auto* sources = &input->sources;
    const auto* semantic = &input->semantic.program;
    auto selection = std::optional<std::span<const CanonicalModulePath>>();
    if (printing_artifacts) {
        selection = sources->owned_modules;
    }
    const auto runtime_test_count = static_cast<std::size_t>(
        std::ranges::count_if(semantic->tests().entries(), [&](const auto& record) noexcept {
            const auto& test = record.value;
            const auto& module_decl = semantic->declarations().module_decl(test.module_id);
            const auto& path =
                semantic->provenance().module_record(module_decl.provenance_module).path;
            return test.execution != TestExecution::Static
                && std::ranges::contains(sources->owned_modules, path);
        })
    );
    auto artifacts = [&]() noexcept -> std::expected<GeneratedArtifactSet, std::string> {
        if (request.command == CompilationCommand::Compile) {
            auto linkage_domain = resolve_linkage_domain(destination);
            if (!linkage_domain) {
                return std::unexpected(std::move(linkage_domain.error()));
            }
            const auto generation = TargetPlanningRequest {
                .test_mode = request.test_mode,
                .linkage_domain = std::move(*linkage_domain),
                .libraries = source_libraries(*sources),
                .test_modules = sources->owned_modules,
            };
            return generate_artifacts(*semantic, generation, selection, timings);
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
            .link_dependencies = {},
        });
        return GeneratedArtifactSet(std::move(entries));
    }();
    if (!artifacts) {
        return std::unexpected(std::move(artifacts.error()));
    }

    const auto writing = TimingScope(timings, TimingStage::ArtifactWriting);
    auto written = destination.visit(
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
        return std::unexpected(std::move(written.error()));
    }
    return CompiledArtifacts {
        .sources = std::move(input->sources),
        .artifacts = std::move(*artifacts),
        .runtime_test_count = runtime_test_count,
        .cacheable = input->semantic.cacheable,
    };
}

} // namespace

auto run_compilation_command(
    std::span<const std::string> args,
    CompilationCommand command,
    std::span<const ExecutionNativeBinding> native_bindings
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
                "Options:\n"
                "      --no-query                  Leave Craft queries to the build consumer\n"
                "      --timings                   Show total and stage timings on stderr\n"
                "  -h, --help                      Show this help\n"
                "\n"
                "With no inputs, directory output contains only toolchain resources.\n"
            );
        }
        std::print(
            "\n"
            "Positional sources select roots; imports discover project and Craft dependencies.\n"
            "A checked Craft artifact directory can accompany new sources.\n"
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
        return 0;
    };
    const auto printing_artifacts = request->destination
        && std::holds_alternative<StandardOutputArtifactDestination>(*request->destination);
    const auto write_output = [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
        const auto to_error = stream == ExecutionOutputStream::Error || printing_artifacts;
        std::print(to_error ? std::cerr : std::cout, "{}", bytes);
    };
    auto native_inputs = std::vector<std::string>();
    if (command == CompilationCommand::Compile) {
        std::erase_if(request->input_paths, [&](std::string_view input) noexcept {
            const auto path = path_from_utf8(input);
            const auto extension = path.extension();
            if (extension != ".cpp"
                && extension != ".cc"
                && extension != ".cxx"
                && extension != ".c") {
                return false;
            }
            native_inputs.emplace_back(input);
            return true;
        });
        for (auto& input : native_inputs) {
            auto error = std::error_code();
            const auto path = std::filesystem::canonical(path_from_utf8(input), error);
            if (error || !std::filesystem::is_regular_file(path, error)) {
                return emit_driver_error(std::format("cannot read source '{}'", input));
            }
            input = path_to_generic_utf8(path);
        }
        std::ranges::sort(native_inputs);
        native_inputs.erase(std::ranges::unique(native_inputs).begin(), native_inputs.end());
    }
    const auto crafts = locate_standard_crafts();
    auto result =
        compile_artifacts(crafts, *request, write_output, timings.output(), native_bindings);
    if (!result) {
        return emit_driver_error(result.error());
    }
    if (request->destination) {
        if (const auto* destination =
                std::get_if<DirectoryArtifactDestination>(&*request->destination)) {
            const auto writing = TimingScope(timings.output(), TimingStage::ArtifactWriting);
            if (const auto manifest = write_manifest(
                    destination->root / "carven/manifest.json",
                    crafts,
                    result->sources,
                    result->artifacts,
                    result->runtime_test_count,
                    result->cacheable,
                    request->query_crafts,
                    native_inputs
                );
                !manifest) {
                return emit_driver_error(manifest.error());
            }
        }
    }

    return finish();
}
