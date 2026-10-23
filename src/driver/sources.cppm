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
    std::filesystem::path project_root;
    std::filesystem::path crafts;
    std::vector<std::filesystem::path> library_roots;
    // Available inputs before analysis; loaded dependencies afterwards.
    std::vector<SourceInput> carven;
    std::vector<CanonicalModulePath> standard_modules;
    // Explicit roots select runtime tests and inspection output.
    std::vector<CanonicalModulePath> owned_modules;
    // Retained providers supply these implementations outside this generation.
    std::vector<CanonicalModulePath> external_modules;
};

// Explicit paths select roots. Installed and project Crafts supply dependency
// candidates; only the selected source closure participates in analysis.
auto collect_command_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings
) noexcept -> std::expected<CommandSources, std::string>;

// Explicit toolchain roots support in-process compilation and command discovery.
auto collect_toolchain_sources(
    const std::filesystem::path& crafts,
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    const std::filesystem::path& project_root = "."
) noexcept -> std::expected<CommandSources, std::string>;

// Checked dependencies already supply installed definitions.
auto collect_project_sources(
    std::span<const std::string_view> inputs,
    TimingOutput timings,
    const std::filesystem::path& project_root = "."
) noexcept -> std::expected<CommandSources, std::string>;

// Locate an ordinary project dependency by canonical module identity. The root
// is the canonical project directory already established by source collection.
auto locate_project_source(
    const CanonicalModulePath& module_path,
    const std::filesystem::path& project_root
) noexcept -> std::optional<SourceInput>;

// Each Craft keeps one native identity independently of consumer paths and test
// modes. Retained dependencies keep their external implementation ownership.
auto source_libraries(const CommandSources& sources) noexcept -> std::vector<SourceLibrary>;
