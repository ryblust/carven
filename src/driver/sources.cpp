module carven:driver.sources.impl;

import :backend.generation.request;
import :driver.input_path;
import :driver.process;
import :driver.sources;
import :support.path;
import :support.timing;
import std;

namespace {

auto find_crafts_directory(std::string_view executable) noexcept -> std::filesystem::path {
    const auto program = current_executable_path(executable);
    if (program.empty()) {
        return {};
    }
    auto error = std::error_code();
    auto installed = program.parent_path().parent_path() / "crafts";
    if (std::filesystem::is_directory(installed / "carven", error)) {
        return installed;
    }
    // Development binaries must belong to a Carven source checkout.
    for (auto root = program.parent_path(); root != root.parent_path(); root = root.parent_path()) {
        if (std::filesystem::is_regular_file(root / "src/carven.cppm", error)
            && std::filesystem::is_regular_file(root / "xmake.lua", error)
            && std::filesystem::is_directory(root / "crafts/carven", error)) {
            return root / "crafts";
        }
    }
    return {};
}

enum class CollectedSourceKind {
    Standard,
    Project,
    Explicit,
};

// Installed sources supply references; project discovery and explicit inputs
// supply implementations. Mapped input batches own project source selection.
auto collect_sources(
    std::span<const std::string_view> inputs,
    const std::optional<std::filesystem::path>& crafts,
    std::span<const SourceInput> mapped_inputs
) noexcept -> std::expected<CommandSources, std::string> {
    auto result = CommandSources {
        .crafts = crafts.value_or(std::filesystem::path()),
        .carven = {},
        .native = {},
        .standard_modules = {},
        .owned_modules = {},
    };
    auto seen = std::map<std::filesystem::path, std::optional<CanonicalModulePath>>();
    const auto add = [&](const std::filesystem::path& path,
                         std::string_view logical_path,
                         CollectedSourceKind kind,
                         const CanonicalModulePath* mapped =
                             nullptr) noexcept -> std::expected<void, std::string> {
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
            if (mapped && previous->second != *mapped) {
                return std::unexpected(
                    std::format("conflicting module identities for source '{}'", logical_path)
                );
            }
            if (kind == CollectedSourceKind::Explicit
                && previous->second
                && !std::ranges::contains(result.owned_modules, *previous->second)) {
                result.owned_modules.push_back(*previous->second);
            }
            return {};
        }
        if (path.extension() == ".cv") {
            auto module_path = mapped ? std::expected<CanonicalModulePath, std::string>(*mapped)
                                      : derive_input_module_path(logical_path);
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
        } else {
            seen.emplace(identity, std::nullopt);
            result.native.push_back(path_to_generic_utf8(physical));
        }
        return {};
    };
    const auto roots = std::array {
        crafts ? *crafts / "carven" : std::filesystem::path(),
        std::filesystem::path("crafts"),
    };
    for (auto index = 0uz; index < roots.size(); ++index) {
        if ((index == 0uz && !crafts) || (index == 1uz && !mapped_inputs.empty())) {
            continue;
        }
        const auto& root = roots[index];
        auto error = std::error_code();
        const auto exists = std::filesystem::exists(root, error);
        if (!error && !exists && root == std::filesystem::path("crafts")) {
            continue;
        }
        auto iterator = std::filesystem::recursive_directory_iterator(root, error);
        const auto end = std::filesystem::recursive_directory_iterator();
        auto files = std::vector<std::filesystem::path>();
        while (!error && iterator != end) {
            const auto path = iterator->path();
            if ((path.extension() == ".cv" || path.extension() == ".cpp")
                && iterator->is_regular_file(error)) {
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
            const auto logical = (index == 0 ? std::filesystem::path("crafts/carven")
                                             : std::filesystem::path("crafts"))
                / path.lexically_relative(root);
            const auto kind =
                index == 0uz ? CollectedSourceKind::Standard : CollectedSourceKind::Project;
            if (const auto added = add(path, path_to_generic_utf8(logical), kind); !added) {
                return std::unexpected(added.error());
            }
        }
    }
    for (const auto& input : mapped_inputs) {
        if (const auto added =
                add(path_from_utf8(input.path),
                    input.path,
                    CollectedSourceKind::Explicit,
                    &input.module_path);
            !added) {
            return std::unexpected(added.error());
        }
    }
    for (const auto input : inputs) {
        const auto path = path_from_utf8(input);
        // Discovered files keep their root-relative identity. Other explicit inputs
        // follow the ordinary application or Crafts path convention.
        if (path.extension() != ".cv") {
            return std::unexpected(std::format("input '{}' does not have a .cv extension", input));
        }
        if (const auto added = add(path, input, CollectedSourceKind::Explicit); !added) {
            return std::unexpected(added.error());
        }
    }
    std::ranges::sort(result.carven, {}, &SourceInput::path);
    std::ranges::sort(result.native);
    std::ranges::sort(result.standard_modules);
    std::ranges::sort(result.owned_modules);
    return result;
}

} // namespace

auto parse_source_input(std::string_view value) noexcept
    -> std::expected<SourceInput, std::string> {
    const auto separator = value.find('=');
    if (separator == std::string_view::npos || separator + 1 == value.size()) {
        return std::unexpected("source mapping requires <canonical-module>=<path>");
    }
    auto module = CanonicalModulePath::from_value(value.substr(0, separator));
    if (!module) {
        return std::unexpected("source mapping requires a canonical module identity");
    }
    const auto path = value.substr(separator + 1);
    if (path_from_utf8(path).extension() != ".cv") {
        return std::unexpected("mapped source must have a .cv extension");
    }
    return SourceInput {.path = std::string(path), .module_path = std::move(*module)};
}

auto collect_command_sources(
    std::string_view executable,
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    std::span<const SourceInput> mapped_inputs
) noexcept -> std::expected<CommandSources, std::string> {
    const auto collection = TimingScope(timings, TimingStage::SourceCollection);
    const auto crafts = find_crafts_directory(executable);
    if (crafts.empty()) {
        return std::unexpected(
            "cannot locate Crafts in the Carven installation or source checkout"
        );
    }
    return collect_sources(inputs, crafts, mapped_inputs);
}

auto collect_project_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    std::span<const SourceInput> mapped_inputs
) noexcept -> std::expected<CommandSources, std::string> {
    const auto collection = TimingScope(timings, TimingStage::SourceCollection);
    return collect_sources(inputs, std::nullopt, mapped_inputs);
}

auto standard_source_library(const CommandSources& sources, bool own_all_standard) noexcept
    -> std::optional<SourceLibrary> {
    if (sources.standard_modules.empty()) {
        return std::nullopt;
    }
    auto owned = std::vector<CanonicalModulePath>();
    for (const auto& module : sources.standard_modules) {
        if (own_all_standard || std::ranges::contains(sources.owned_modules, module)) {
            owned.push_back(module);
        }
    }
    return SourceLibrary {
        .modules = sources.standard_modules,
        .linkage_domain = *LinkageDomain::explicit_value("carven:standard"),
        .owned_modules = std::move(owned),
    };
}
