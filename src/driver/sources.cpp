module carven:driver.sources.impl;

import :backend.generation.request;
import :driver.sources;
import :source.module_path;
import :support.path;
import :support.process;
import :support.timing;
import std;

namespace {

enum class CollectedSourceKind {
    Standard,
    Library,
    Explicit,
};

// Installed sources supply available dependencies; explicit inputs select roots.
auto collect_sources(
    std::span<const std::string_view> inputs,
    const std::optional<std::filesystem::path>& crafts,
    const std::filesystem::path& root
) noexcept -> std::expected<CommandSources, std::string> {
    auto root_error = std::error_code();
    const auto project_root = std::filesystem::canonical(root, root_error);
    if (root_error) {
        return std::unexpected(
            std::format("cannot resolve project root: {}", root_error.message())
        );
    }
    auto result = CommandSources {
        .project_root = project_root,
        .crafts = crafts.value_or(std::filesystem::path()),
        .library_roots = {},
        .carven = {},
        .standard_modules = {},
        .owned_modules = {},
        .external_modules = {},
    };
    auto seen = std::map<std::filesystem::path, CanonicalModulePath>();
    const auto add = [&](const std::filesystem::path& path,
                         std::string_view logical_path,
                         CollectedSourceKind kind) noexcept -> std::expected<void, std::string> {
        auto error = std::error_code();
        const auto physical =
            std::filesystem::absolute(path.is_absolute() ? path : project_root / path, error)
                .lexically_normal();
        const auto identity =
            error ? std::filesystem::path() : std::filesystem::canonical(physical, error);
        if (error) {
            return std::unexpected(
                std::format(
                    "cannot read source file '{}': {}",
                    path_to_generic_utf8(path),
                    error.message()
                )
            );
        }
        if (const auto previous = seen.find(identity); previous != seen.end()) {
            if (kind == CollectedSourceKind::Explicit
                && !std::ranges::contains(result.owned_modules, previous->second)) {
                result.owned_modules.push_back(previous->second);
            }
            return {};
        }
        const auto logical = kind == CollectedSourceKind::Standard
            ? path_from_utf8(logical_path)
            : identity.lexically_relative(project_root);
        if (kind == CollectedSourceKind::Explicit
            && (logical.empty() || logical.is_absolute() || *logical.begin() == "..")) {
            return std::unexpected(
                std::format("input '{}' escapes the project root", path_to_generic_utf8(path))
            );
        }
        auto module_path = CanonicalModulePath::from_source_path(path_to_generic_utf8(logical));
        if (!module_path) {
            return std::unexpected(module_path.error());
        }
        seen.emplace(identity, *module_path);
        if (kind == CollectedSourceKind::Standard) {
            result.standard_modules.push_back(*module_path);
        } else if (kind == CollectedSourceKind::Explicit) {
            result.owned_modules.push_back(*module_path);
        }
        result.carven.push_back({
            .path = path_to_generic_utf8(physical),
            .module_path = std::move(*module_path),
        });
        return {};
    };
    const auto collect_library =
        [&](const std::filesystem::path& directory,
            CollectedSourceKind kind) noexcept -> std::expected<void, std::string> {
        auto error = std::error_code();
        const auto identity = std::filesystem::weakly_canonical(directory, error);
        if (error) {
            return std::unexpected(
                std::format("cannot resolve Crafts directory: {}", error.message())
            );
        }
        result.library_roots.push_back(identity);
        auto iterator = std::filesystem::recursive_directory_iterator(directory, error);
        if (kind == CollectedSourceKind::Library && error == std::errc::no_such_file_or_directory) {
            return {};
        }
        const auto end = std::filesystem::recursive_directory_iterator();
        auto files = std::vector<std::filesystem::path>();
        while (!error && iterator != end) {
            const auto path = iterator->path();
            if (path.extension() == ".cv" && iterator->is_regular_file(error)) {
                files.push_back(path);
            }
            if (!error) {
                iterator.increment(error);
            }
        }
        if (error) {
            return std::unexpected(
                std::format(
                    "cannot scan Crafts directory '{}': {}",
                    path_to_generic_utf8(directory),
                    error.message()
                )
            );
        }
        std::ranges::sort(files);
        for (const auto& path : files) {
            const auto logical = kind == CollectedSourceKind::Standard
                ? std::filesystem::path("crafts/carven") / path.lexically_relative(directory)
                : path.lexically_relative(project_root);
            if (const auto added = add(path, path_to_generic_utf8(logical), kind); !added) {
                return std::unexpected(added.error());
            }
        }
        return {};
    };
    if (crafts) {
        if (const auto collected =
                collect_library(*crafts / "carven", CollectedSourceKind::Standard);
            !collected) {
            return std::unexpected(collected.error());
        }
    }
    if (const auto collected =
            collect_library(project_root / "crafts", CollectedSourceKind::Library);
        !collected) {
        return std::unexpected(collected.error());
    }
    for (const auto input : inputs) {
        const auto path = path_from_utf8(input);
        // Known installed files keep their identity; all other paths are project-relative.
        if (path.extension() != ".cv") {
            return std::unexpected(std::format("input '{}' does not have a .cv extension", input));
        }
        if (const auto added = add(path, {}, CollectedSourceKind::Explicit); !added) {
            return std::unexpected(added.error());
        }
    }
    std::ranges::sort(result.carven, {}, &SourceInput::path);
    std::ranges::sort(result.standard_modules);
    std::ranges::sort(result.owned_modules);
    return result;
}

} // namespace

auto locate_standard_crafts() noexcept -> std::filesystem::path {
    const auto program = process::executable();
    if (!program) {
        return {};
    }
    auto error = std::error_code();
    auto root = program->parent_path().parent_path() / "crafts";
    if (std::filesystem::is_directory(root / "carven", error)) {
        return root;
    }
    for (auto directory = program->parent_path(); directory != directory.parent_path();
         directory = directory.parent_path()) {
        if (std::filesystem::is_regular_file(directory / "src/carven.cppm", error)
            && std::filesystem::is_directory(directory / "crafts/carven", error)) {
            return directory / "crafts";
        }
    }
    return {};
}

auto locate_project_source(
    const CanonicalModulePath& module_path,
    const std::filesystem::path& project_root
) noexcept -> std::optional<SourceInput> {
    if (module_path.module_domain_prefix().craft_name()) {
        return std::nullopt;
    }
    auto relative = std::filesystem::path();
    for (const auto& component : module_path.components()) {
        relative /= path_from_utf8(component);
    }
    relative += ".cv";
    auto error = std::error_code();
    const auto physical = std::filesystem::canonical(project_root / relative, error);
    // A filesystem alias does not introduce another canonical module identity.
    if (error || physical.lexically_relative(project_root) != relative) {
        return std::nullopt;
    }
    return SourceInput {.path = path_to_generic_utf8(physical), .module_path = module_path};
}

auto collect_command_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings
) noexcept -> std::expected<CommandSources, std::string> {
    return collect_toolchain_sources(locate_standard_crafts(), inputs, timings);
}

auto collect_toolchain_sources(
    const std::filesystem::path& crafts,
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    const std::filesystem::path& project_root
) noexcept -> std::expected<CommandSources, std::string> {
    const auto collection = TimingScope(timings, TimingStage::SourceCollection);
    if (crafts.empty()) {
        return std::unexpected(
            "incomplete Carven installation: cannot locate Crafts beside the compiler"
        );
    }
    return collect_sources(inputs, crafts, project_root);
}

auto collect_project_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    const std::filesystem::path& project_root
) noexcept -> std::expected<CommandSources, std::string> {
    const auto collection = TimingScope(timings, TimingStage::SourceCollection);
    return collect_sources(inputs, std::nullopt, project_root);
}

auto source_libraries(const CommandSources& sources) noexcept -> std::vector<SourceLibrary> {
    auto libraries = std::map<std::string, SourceLibrary, std::less<>>();
    for (const auto& input : sources.carven) {
        const auto domain = input.module_path.module_domain_prefix();
        const auto craft = domain.craft_name();
        if (!craft) {
            continue;
        }
        const auto entry =
            libraries
                .try_emplace(
                    std::string(*craft),
                    SourceLibrary {
                        .modules = {},
                        .linkage_domain =
                            *LinkageDomain::explicit_value(std::format("carven:craft:{}", *craft)),
                        .owned_modules = {},
                    }
                )
                .first;
        auto& library = entry->second;
        library.modules.push_back(input.module_path);
        if (!std::ranges::contains(sources.external_modules, input.module_path)) {
            library.owned_modules.push_back(input.module_path);
        }
    }
    auto result = std::vector<SourceLibrary>();
    for (auto& entry : libraries) {
        result.push_back(std::move(entry.second));
    }
    return result;
}
