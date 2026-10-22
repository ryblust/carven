module;


#include <carven/std/compiler/native.hpp>

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
        inputs.push_back({.path = paths.back(), .module_path = source.module_path.value()});
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
    TimingOutput timings
) noexcept -> std::expected<CompiledArtifacts, std::string> {
    auto input = load_compilation_input(crafts, request, output, timings);
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
                .library = standard_source_library(*sources),
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

auto run_compilation_command(std::span<const std::string> args, CompilationCommand command) noexcept
    -> int {
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
                "      --timings                   Show total and stage timings on stderr\n"
                "  -h, --help                      Show this help\n"
            );
        }
        std::print(
            "\n"
            "Installed Crafts supply dependencies; positional sources select the complete\n"
            "owned source set, including any project Crafts used by the program.\n"
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
    auto result =
        compile_artifacts(locate_standard_crafts(), *request, write_output, timings.output());
    if (!result) {
        return emit_driver_error(result.error());
    }
    if (request->destination) {
        if (const auto* destination =
                std::get_if<DirectoryArtifactDestination>(&*request->destination)) {
            const auto writing = TimingScope(timings.output(), TimingStage::ArtifactWriting);
            if (const auto manifest = write_manifest(
                    destination->root / "carven/manifest.json",
                    result->sources,
                    result->artifacts,
                    result->runtime_test_count,
                    result->cacheable
                );
                !manifest) {
                return emit_driver_error(manifest.error());
            }
        }
    }

    return finish();
}

// Match the ordinary header declarations' global-module ownership.
extern "C++" {

namespace carven::standard::compiler {

auto CompilationResult::take_error() noexcept -> runtime::String {
    return std::move(error);
}

auto CompilationResult::take_implementations() noexcept -> runtime::Vector<Implementation> {
    return std::move(implementations);
}

auto CompilationResult::take_include_dirs() noexcept -> runtime::Vector<runtime::String> {
    return std::move(include_dirs);
}

auto CompilationResult::take_native_providers() noexcept -> runtime::Vector<runtime::String> {
    return std::move(native_providers);
}

auto compile(runtime::Slice<runtime::String> sources, std::string_view destination) noexcept
    -> CompilationResult {
    const auto failed = [](std::string message) static noexcept -> CompilationResult {
        return {
            .success = false,
            .cacheable = false,
            .error = runtime::String::from_utf8(std::move(message)),
            .implementations = {},
            .include_dirs = {},
            .native_providers = {},
        };
    };
    if (destination.empty()) {
        return failed("output path is empty");
    }
    const auto crafts = locate_standard_crafts();
    if (crafts.empty()) {
        return failed("cannot locate Carven Crafts");
    }
    auto native_providers = runtime::Vector<runtime::String>();
    auto error = std::error_code();
    auto candidates = std::vector<std::filesystem::path>();
    for (auto iterator = std::filesystem::recursive_directory_iterator(crafts / "carven", error);
         !error && iterator != std::filesystem::recursive_directory_iterator();
         iterator.increment(error)) {
        if (iterator->is_regular_file(error) && iterator->path().extension() == ".cpp") {
            candidates.push_back(iterator->path());
        }
    }
    if (error) {
        return failed(std::format("cannot inspect Carven Crafts: {}", error.message()));
    }
    std::ranges::sort(candidates);
    for (const auto& path : candidates) {
        native_providers.push(runtime::String::from_utf8(path_to_generic_utf8(path)));
    }
    auto include_dirs = runtime::Vector<runtime::String>();
    include_dirs.push(runtime::String::from_utf8(path_to_generic_utf8(crafts)));
    if (sources.empty()) {
        return {
            .success = true,
            .cacheable = true,
            .error = {},
            .implementations = {},
            .include_dirs = std::move(include_dirs),
            .native_providers = std::move(native_providers),
        };
    }
    auto paths = std::vector<std::string_view>();
    paths.reserve(sources.size());
    for (const auto& source : sources) {
        paths.push_back(source.as_str());
    }
    const auto root = path_from_utf8(destination);
    const auto request = CompilationOptions {
        .command = CompilationCommand::Compile,
        .destination = DirectoryArtifactDestination {.root = root},
        .test_mode = TestGenerationMode::None,
        .input_paths = std::move(paths),
        .timings = false,
    };
    const auto output = [](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
        std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
    };
    const auto compiled = compile_artifacts(crafts, request, output, {});
    if (!compiled) {
        return failed(compiled.error());
    }
    auto implementations = runtime::Vector<Implementation>();
    for (const auto& artifact : compiled->artifacts.entries()) {
        if (artifact.role != GeneratedArtifactRole::ModuleImplementation) {
            continue;
        }
        if (!artifact.source_path) {
            invariant_violation("module implementation has no source owner");
        }
        auto error = std::error_code();
        const auto source =
            std::filesystem::weakly_canonical(path_from_utf8(*artifact.source_path), error);
        if (error) {
            return failed(
                std::format(
                    "cannot resolve source '{}': {}",
                    *artifact.source_path,
                    error.message()
                )
            );
        }
        auto dependencies = runtime::Vector<runtime::String>();
        dependencies.reserve(artifact.link_dependencies.size());
        for (const auto& dependency : artifact.link_dependencies) {
            dependencies.push(
                runtime::String::from_utf8(path_to_generic_utf8(root / path_from_utf8(dependency)))
            );
        }
        implementations.push({
            .file = runtime::String::from_utf8(
                path_to_generic_utf8(root / path_from_utf8(artifact.logical_path))
            ),
            .source = runtime::String::from_utf8(path_to_generic_utf8(source)),
            .dependencies = std::move(dependencies),
        });
    }
    return {
        .success = true,
        .cacheable = compiled->cacheable,
        .error = {},
        .implementations = std::move(implementations),
        .include_dirs = std::move(include_dirs),
        .native_providers = std::move(native_providers),
    };
}

} // namespace carven::standard::compiler
}
