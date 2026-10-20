module carven:driver.sources;

import :backend.generation.request;
import :source.module_path;
import :support.timing;
import std;

// Locate resources paired with the running compiler.
auto locate_standard_crafts() noexcept -> std::filesystem::path;

// Collected physical inputs use absolute paths; module identity is independent.
struct SourceInput final {
    std::string path;
    CanonicalModulePath module_path;
};

struct CommandSources final {
    std::filesystem::path crafts;
    // Available inputs before analysis; loaded dependencies afterwards.
    std::vector<SourceInput> carven;
    std::vector<CanonicalModulePath> standard_modules;
    // Explicit roots select runtime tests and inspection output.
    std::vector<CanonicalModulePath> owned_modules;
    // Retained providers supply these implementations outside this generation.
    std::vector<CanonicalModulePath> external_modules;
};

// Explicit paths select the complete owned source set. Installed Crafts supply
// dependencies and retain their root-relative identity when explicitly selected.
auto collect_command_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings
) noexcept -> std::expected<CommandSources, std::string>;

// Explicit toolchain roots support in-process compilation and command discovery.
auto collect_toolchain_sources(
    const std::filesystem::path& crafts,
    std::span<const std::string_view> inputs,
    TimingOutput timings
) noexcept -> std::expected<CommandSources, std::string>;

// Checked dependencies already supply installed definitions.
auto collect_project_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings
) noexcept -> std::expected<CommandSources, std::string>;

// Installed sources keep a compiler-defined native identity independently of
// consumer paths, test modes, and native compilation settings.
auto standard_source_library(const CommandSources& sources) noexcept
    -> std::optional<SourceLibrary>;
