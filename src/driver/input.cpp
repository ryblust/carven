module carven:driver.input.impl;

import :compiler.artifact;
import :driver.analysis;
import :driver.input;
import :driver.options;
import :driver.sources;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.artifact;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.module_path;
import :source.provenance;
import :support.binary;
import :support.file;
import :support.path;
import :support.timing;
import std;

namespace {

auto binary_error(const BinaryError& error) noexcept -> std::string {
    return std::format("invalid semantic artifact: {} at byte {}", error.message, error.offset);
}

auto source_context(
    const CompilationArtifact& artifact,
    CompilationProvenanceView provenance
) noexcept -> std::expected<CommandSources, std::string> {
    const auto modules = provenance.module_records();
    if (artifact.modules.size() != modules.size()) {
        return std::unexpected("compilation ownership does not match semantic modules");
    }
    auto error = std::error_code();
    const auto project_root = std::filesystem::canonical(".", error);
    if (error) {
        return std::unexpected(std::format("cannot resolve project root: {}", error.message()));
    }
    auto sources = CommandSources {
        .project_root = project_root,
        .crafts = {},
        .library_roots = {},
        .carven = {},
        .standard_modules = {},
        .owned_modules = {},
        .external_modules = {},
    };
    for (const auto [index, module_record] : std::views::enumerate(modules)) {
        const auto origin = provenance.source_snapshot(module_record.source_id).display_origin();
        sources.carven.push_back({.path = std::string(origin), .module_path = module_record.path});
        if (artifact.modules[index].owned) {
            sources.owned_modules.push_back(module_record.path);
        }
        if (artifact.modules[index].standard) {
            sources.standard_modules.push_back(module_record.path);
        }
        if (artifact.modules[index].external) {
            sources.external_modules.push_back(module_record.path);
        }
    }
    return sources;
}

} // namespace

auto load_compilation_input(
    const std::filesystem::path& crafts,
    const CompilationOptions& options,
    ExecutionOutput output,
    TimingOutput timings,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> std::expected<CompilationInput, std::string> {
    auto sources = CommandSources {
        .project_root = {},
        .crafts = {},
        .library_roots = {},
        .carven = {},
        .standard_modules = {},
        .owned_modules = {},
        .external_modules = {},
    };
    auto source_paths = std::vector<std::string_view>();
    auto artifact_root = std::optional<std::filesystem::path>();
    auto seed = std::optional<CheckedDefinitions>();
    for (const auto input : options.input_paths) {
        const auto path = path_from_utf8(input);
        auto error = std::error_code();
        if (!std::filesystem::is_directory(path, error)) {
            source_paths.push_back(input);
            continue;
        }
        const auto identity = std::filesystem::canonical(path, error);
        if (error) {
            return std::unexpected(
                std::format("cannot read artifacts '{}': {}", input, error.message())
            );
        }
        if (artifact_root && *artifact_root != identity) {
            return std::unexpected("one artifact dependency closure is accepted");
        }
        artifact_root = identity;
    }
    if (artifact_root) {
        const auto semantic_path = *artifact_root / "carven/semantic";
        auto storage = read_file(semantic_path, compilation_binary_limits.bytes);
        if (!storage) {
            return std::unexpected(
                std::format("cannot read semantic artifact: {}", storage.error().code.message())
            );
        }
        const auto bytes = std::as_bytes(std::span(storage->data(), storage->size()));
        auto artifact = read_compilation_artifact(bytes);
        if (!artifact) {
            return std::unexpected(binary_error(artifact.error()));
        }
        if (source_paths.empty()) {
            auto program = decode_semir_program(artifact->program);
            if (!program) {
                return std::unexpected(binary_error(program.error()));
            }
            auto context = source_context(*artifact, program->provenance());
            if (!context) {
                return std::unexpected(context.error());
            }
            return CompilationInput {
                .sources = std::move(*context),
                .semantic =
                    CheckedUnit {
                        .program = std::move(*program),
                        .definitions = {},
                        .cacheable = artifact->cacheable,
                    },
                .artifact = options.command == CompilationCommand::Check && options.destination
                    ? std::move(*storage)
                    : std::string(),
            };
        }
        auto definitions = decode_checked_definitions(artifact->definitions);
        if (!definitions) {
            return std::unexpected(binary_error(definitions.error()));
        }
        auto context = source_context(*artifact, definitions->provenance_view());
        if (!context) {
            return std::unexpected(context.error());
        }
        if (!std::ranges::all_of(context->owned_modules, [](const auto& path) static noexcept {
                return path.module_domain_prefix().craft_name().has_value();
            })) {
            return std::unexpected("new sources require a Craft artifact dependency");
        }
        sources = std::move(*context);
        seed.emplace(std::move(*definitions));
        sources.owned_modules.clear();
        sources.external_modules.clear();
        for (const auto& input : sources.carven) {
            sources.external_modules.push_back(input.module_path);
        }
    }
    auto collected = collect_toolchain_sources(crafts, source_paths, timings);
    if (!collected) {
        return std::unexpected(collected.error());
    }
    if (seed) {
        // Retained definitions own these modules. Inventory candidates must not
        // reload them; an explicit root remains visible so redefinition is rejected.
        std::erase_if(collected->carven, [&](const auto& input) noexcept {
            return !std::ranges::contains(collected->owned_modules, input.module_path)
                && std::ranges::any_of(sources.carven, [&](const auto& retained) noexcept {
                       return retained.module_path == input.module_path;
                   });
        });
    }
    const auto retain_definitions =
        options.command == CompilationCommand::Check && options.destination.has_value();
    auto checked = load_and_analyze_checked_sources(
        *collected,
        std::move(seed),
        retain_definitions,
        output,
        native_bindings,
        timings
    );
    if (!checked) {
        return std::unexpected("source analysis failed");
    }
    sources.carven.append_range(collected->carven);
    sources.owned_modules.append_range(collected->owned_modules);
    sources.standard_modules.append_range(collected->standard_modules);
    sources.project_root = std::move(collected->project_root);
    sources.crafts = std::move(collected->crafts);
    sources.library_roots = std::move(collected->library_roots);
    return CompilationInput {
        .sources = std::move(sources),
        .semantic = std::move(*checked),
        .artifact = {},
    };
}

auto encode_compilation_input(CompilationInput& input) noexcept
    -> std::expected<std::string, std::string> {
    if (!input.artifact.empty()) {
        return std::move(input.artifact);
    }
    auto modules = std::vector<CompilationModuleOwnership>();
    for (const auto& module_record : input.semantic.program.provenance().module_records()) {
        modules.push_back({
            .owned = std::ranges::contains(input.sources.owned_modules, module_record.path),
            .standard = std::ranges::contains(input.sources.standard_modules, module_record.path),
            .external = std::ranges::contains(input.sources.external_modules, module_record.path),
        });
    }
    auto semantic = encode_compilation_artifact(input.semantic, modules);
    if (!semantic) {
        return std::unexpected(binary_error(semantic.error()));
    }
    return std::string(reinterpret_cast<const char*>(semantic->data()), semantic->size());
}
