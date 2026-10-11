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

auto parse_source_input(std::string_view value) noexcept -> std::expected<SourceInput, std::string>;

// The CLI discovers fixed Crafts roots unless the build supplies a complete batch.
auto collect_command_sources(
    std::string_view executable,
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    bool explicit_inputs = false,
    std::span<const SourceInput> mapped_inputs = {}
) noexcept -> std::expected<CommandSources, std::string>;
