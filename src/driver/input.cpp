module carven:driver.input.impl;

import :compiler.artifact;
import :driver.analysis;
import :driver.diagnostic;
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
    auto sources = CommandSources {};
    for (const auto [index, module] : std::views::enumerate(modules)) {
        const auto origin = provenance.source_snapshot(module.source_id).display_origin();
        sources.carven.push_back({.path = std::string(origin), .module_path = module.path});
        if (artifact.modules[index].owned) {
            sources.owned_modules.push_back(module.path);
        }
        if (artifact.modules[index].standard) {
            sources.standard_modules.push_back(module.path);
        }
    }
    return sources;
}

} // namespace

auto load_compilation_input(
    std::string_view executable,
    const CompilationOptions& options,
    ExecutionOutput output,
    TimingOutput timings
) noexcept -> std::optional<CompilationInput> {
    auto sources = CommandSources {};
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
            emit_driver_error(
                std::format("cannot read artifacts '{}': {}", input, error.message())
            );
            return std::nullopt;
        }
        if (artifact_root && *artifact_root != identity) {
            emit_driver_error("one artifact dependency closure is accepted");
            return std::nullopt;
        }
        artifact_root = identity;
    }
    if (artifact_root) {
        const auto semantic_path = *artifact_root / "carven/semantic";
        auto storage = read_file(semantic_path, compilation_binary_limits.bytes);
        if (!storage) {
            emit_driver_error(
                std::format("cannot read semantic artifact: {}", storage.error().code.message())
            );
            return std::nullopt;
        }
        const auto bytes = std::as_bytes(std::span(storage->data(), storage->size()));
        auto artifact = read_compilation_artifact(bytes);
        if (!artifact) {
            emit_driver_error(binary_error(artifact.error()));
            return std::nullopt;
        }
        if (source_paths.empty() && options.mapped_inputs.empty()) {
            auto program = decode_semir_program(artifact->program);
            if (!program) {
                emit_driver_error(binary_error(program.error()));
                return std::nullopt;
            }
            auto context = source_context(*artifact, program->provenance());
            if (!context) {
                emit_driver_error(context.error());
                return std::nullopt;
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
        if (!std::ranges::all_of(artifact->modules, [](const auto& module) static noexcept {
                return !module.owned || module.standard;
            })) {
            emit_driver_error("new sources require a standard-library artifact dependency");
            return std::nullopt;
        }
        auto definitions = decode_checked_definitions(artifact->definitions);
        if (!definitions) {
            emit_driver_error(binary_error(definitions.error()));
            return std::nullopt;
        }
        auto context = source_context(*artifact, definitions->provenance_view());
        if (!context) {
            emit_driver_error(context.error());
            return std::nullopt;
        }
        sources = std::move(*context);
        seed.emplace(std::move(*definitions));
        sources.owned_modules.clear();
    }
    auto collected = artifact_root
        ? collect_project_sources(source_paths, timings, options.mapped_inputs)
        : collect_command_sources(executable, source_paths, timings, options.mapped_inputs);
    if (!collected) {
        emit_driver_error(collected.error());
        return std::nullopt;
    }
    sources.carven.append_range(collected->carven);
    sources.owned_modules.append_range(collected->owned_modules);
    sources.standard_modules.append_range(collected->standard_modules);
    sources.crafts = std::move(collected->crafts);
    sources.native = std::move(collected->native);
    const auto retain_definitions =
        options.command == CompilationCommand::Check
        && options.destination
        && !sources.owned_modules.empty()
        && std::ranges::all_of(sources.owned_modules, [&](const auto& module) noexcept {
               return std::ranges::contains(sources.standard_modules, module);
           });
    auto checked = load_and_analyze_checked_sources(
        collected->carven,
        std::move(seed),
        retain_definitions,
        output,
        timings
    );
    if (!checked) {
        return std::nullopt;
    }
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
    for (const auto& module : input.semantic.program.provenance().module_records()) {
        modules.push_back({
            .owned = std::ranges::contains(input.sources.owned_modules, module.path),
            .standard = std::ranges::contains(input.sources.standard_modules, module.path),
        });
    }
    auto semantic = encode_compilation_artifact(input.semantic, modules);
    if (!semantic) {
        return std::unexpected(binary_error(semantic.error()));
    }
    return std::string(reinterpret_cast<const char*>(semantic->data()), semantic->size());
}
