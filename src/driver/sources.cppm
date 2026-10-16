module carven:driver.sources;

import :backend.generation.request;
import :source.module_path;
import :support.timing;
import std;

// Collected physical inputs use absolute paths; module identity is independent.
struct SourceInput final {
    std::string path;
    CanonicalModulePath module_path;
};

struct CommandSources final {
    std::filesystem::path crafts;
    std::vector<SourceInput> carven;
    std::vector<std::string> native;
    std::vector<CanonicalModulePath> standard_modules;
    std::vector<CanonicalModulePath> owned_modules;
};

auto parse_source_input(std::string_view value) noexcept -> std::expected<SourceInput, std::string>;

// The CLI loads installed Crafts; mapped inputs select the project source batch.
auto collect_command_sources(
    std::string_view executable,
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    std::span<const SourceInput> mapped_inputs = {}
) noexcept -> std::expected<CommandSources, std::string>;

// Checked dependencies supply installed definitions. Collect only owned project
// inputs, preserving project Crafts discovery for positional invocations.
auto collect_project_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    std::span<const SourceInput> mapped_inputs = {}
) noexcept -> std::expected<CommandSources, std::string>;

// Installed sources keep a compiler-defined native identity independently of
// consumer paths, test modes, and native compilation settings.
auto standard_source_library(const CommandSources& sources, bool own_all_standard = false) noexcept
    -> std::optional<SourceLibrary>;
