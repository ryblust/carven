module carven:driver.sources.impl;

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
    if (std::filesystem::is_regular_file(installed / "carven/runtime/runtime.hpp", error)) {
        return installed;
    }
    // Development binaries must belong to a Carven source checkout.
    for (auto root = program.parent_path(); root != root.parent_path(); root = root.parent_path()) {
        if (std::filesystem::is_regular_file(root / "src/carven.cppm", error)
            && std::filesystem::is_regular_file(root / "xmake.lua", error)
            && std::filesystem::is_regular_file(
                root / "crafts/carven/runtime/runtime.hpp",
                error
            )) {
            return root / "crafts";
        }
    }
    return {};
}

// Application inputs remain explicit. Only the two fixed Crafts roots are
// collected; imports, manifests, and parent directories are not searched.
auto collect_crafts_sources(
    std::span<const std::string_view> inputs,
    const std::filesystem::path& crafts
) noexcept -> std::expected<CommandSources, std::string> {
    auto result = CommandSources {.crafts = crafts, .carven = {}, .native = {}};
    auto seen = std::set<std::filesystem::path>();
    const auto add =
        [&](const std::filesystem::path& path,
            std::string_view logical_path) noexcept -> std::expected<void, std::string> {
        auto error = std::error_code();
        const auto identity = std::filesystem::canonical(path, error);
        if (error) {
            return std::unexpected(
                std::format(
                    "cannot read source file '{}': {}",
                    path_to_generic_utf8(path),
                    error.message()
                )
            );
        }
        if (seen.contains(identity)) {
            return {};
        }
        if (path.extension() == ".cv") {
            auto module_path = derive_input_module_path(logical_path);
            if (!module_path) {
                return std::unexpected(module_path.error());
            }
            result.carven.push_back({
                .path = path_to_generic_utf8(path),
                .module_path = std::move(*module_path),
            });
        } else {
            result.native.push_back(path_to_generic_utf8(path));
        }
        seen.insert(identity);
        return {};
    };
    const auto roots = std::array {crafts / "carven", std::filesystem::path("crafts")};
    for (auto index = 0uz; index < roots.size(); ++index) {
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
            if (const auto added = add(path, path_to_generic_utf8(logical)); !added) {
                return std::unexpected(added.error());
            }
        }
    }
    for (const auto input : inputs) {
        const auto path = path_from_utf8(input);
        // Discovered files keep their root-relative identity. Other explicit inputs
        // follow the ordinary application or Crafts path convention.
        if (path.extension() != ".cv") {
            return std::unexpected(std::format("input '{}' does not have a .cv extension", input));
        }
        if (!path.is_absolute()) {
            if (const auto module_path = derive_input_module_path(input); !module_path) {
                return std::unexpected(module_path.error());
            }
        }
        if (const auto added = add(path, input); !added) {
            return std::unexpected(added.error());
        }
    }
    std::ranges::sort(result.carven, {}, &SourceInput::path);
    std::ranges::sort(result.native);
    return result;
}

} // namespace

auto collect_command_sources(
    std::string_view executable,
    std::span<const std::string_view> inputs,
    TimingRecorder* timings
) noexcept -> std::expected<CommandSources, std::string> {
    const auto collection = TimingScope(timings, TimingStage::SourceCollection);
    const auto crafts = find_crafts_directory(executable);
    if (crafts.empty()) {
        return std::unexpected(
            "cannot locate Crafts in the Carven installation or source checkout"
        );
    }
    return collect_crafts_sources(inputs, crafts);
}
