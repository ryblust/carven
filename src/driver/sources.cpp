module;

#include <carven/std/process/native.hpp>

module carven:driver.sources.impl;

import :backend.generation.request;
import :driver.sources;
import :source.module_path;
import :support.path;
import :support.timing;
import std;

namespace {

enum class CollectedSourceKind {
    Standard,
    Explicit,
};

// Installed sources supply available dependencies; explicit inputs select roots.
auto collect_sources(
    std::span<const std::string_view> inputs,
    const std::optional<std::filesystem::path>& crafts
) noexcept -> std::expected<CommandSources, std::string> {
    auto result = CommandSources {
        .crafts = crafts.value_or(std::filesystem::path()),
        .carven = {},
        .standard_modules = {},
        .owned_modules = {},
        .external_modules = {},
    };
    auto root_error = std::error_code();
    const auto project_root = std::filesystem::canonical(".", root_error);
    if (root_error) {
        return std::unexpected(
            std::format("cannot resolve working directory: {}", root_error.message())
        );
    }
    auto seen = std::map<std::filesystem::path, CanonicalModulePath>();
    const auto add = [&](const std::filesystem::path& path,
                         std::string_view logical_path,
                         CollectedSourceKind kind) noexcept -> std::expected<void, std::string> {
        auto error = std::error_code();
        const auto physical = std::filesystem::absolute(path, error).lexically_normal();
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
                std::format("input '{}' escapes the working directory", path_to_generic_utf8(path))
            );
        }
        auto module_path = CanonicalModulePath::from_source_path(path_to_generic_utf8(logical));
        if (!module_path) {
            return std::unexpected(module_path.error());
        }
        seen.emplace(identity, *module_path);
        if (kind == CollectedSourceKind::Standard) {
            result.standard_modules.push_back(*module_path);
        } else {
            result.owned_modules.push_back(*module_path);
        }
        result.carven.push_back({
            .path = path_to_generic_utf8(physical),
            .module_path = std::move(*module_path),
        });
        return {};
    };
    if (crafts) {
        const auto root = *crafts / "carven";
        auto error = std::error_code();
        auto iterator = std::filesystem::recursive_directory_iterator(root, error);
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
                    path_to_generic_utf8(root),
                    error.message()
                )
            );
        }
        std::ranges::sort(files);
        for (const auto& path : files) {
            const auto logical =
                std::filesystem::path("crafts/carven") / path.lexically_relative(root);
            if (const auto added =
                    add(path, path_to_generic_utf8(logical), CollectedSourceKind::Standard);
                !added) {
                return std::unexpected(added.error());
            }
        }
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
    const auto executable = carven::standard::process::executable();
    if (executable.code != 0) {
        return {};
    }
    auto error = std::error_code();
    const auto program =
        std::filesystem::canonical(path_from_utf8(executable.text.as_str()), error);
    if (error) {
        return {};
    }
    const auto root = program.parent_path().parent_path() / "crafts";
    if (std::filesystem::is_directory(root / "carven", error)) {
        return root;
    }
    for (auto directory = program.parent_path(); directory != directory.parent_path();
         directory = directory.parent_path()) {
        if (std::filesystem::is_regular_file(directory / "src/carven.cppm", error)
            && std::filesystem::is_directory(directory / "crafts/carven", error)) {
            return directory / "crafts";
        }
    }
    return {};
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
    TimingOutput timings
) noexcept -> std::expected<CommandSources, std::string> {
    const auto collection = TimingScope(timings, TimingStage::SourceCollection);
    if (crafts.empty()) {
        return std::unexpected(
            "incomplete Carven installation: cannot locate Crafts beside the compiler"
        );
    }
    return collect_sources(inputs, crafts);
}

auto collect_project_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings
) noexcept -> std::expected<CommandSources, std::string> {
    const auto collection = TimingScope(timings, TimingStage::SourceCollection);
    return collect_sources(inputs, std::nullopt);
}

auto standard_source_library(const CommandSources& sources) noexcept
    -> std::optional<SourceLibrary> {
    if (sources.standard_modules.empty()) {
        return std::nullopt;
    }
    auto owned = std::vector<CanonicalModulePath>();
    for (const auto& module_path : sources.standard_modules) {
        if (!std::ranges::contains(sources.external_modules, module_path)) {
            owned.push_back(module_path);
        }
    }
    return SourceLibrary {
        .modules = sources.standard_modules,
        .linkage_domain = *LinkageDomain::explicit_value("carven:standard"),
        .owned_modules = std::move(owned),
    };
}
