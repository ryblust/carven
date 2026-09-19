module carven:driver.sources;

import :source.module_path;
import :support.timing;
import std;

struct SourceInput final {
    std::string path;
    CanonicalModulePath module_path;
};

struct CommandSources final {
    std::filesystem::path crafts;
    std::vector<SourceInput> carven;
    std::vector<std::string> native;
};

// Fixed toolchain and working-directory Crafts roots; other inputs remain explicit.
auto collect_command_sources(
    std::string_view executable,
    std::span<const std::string_view> inputs,
    TimingRecorder* timings
) noexcept -> std::expected<CommandSources, std::string>;
