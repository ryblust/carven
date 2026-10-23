module carven:compiler.manifest;

import :artifacts;
import std;

struct CompilationManifestInput final {
    std::string_view path;
    std::string_view module_path;
};

struct CompilationManifestResources final {
    std::vector<std::string> include_dirs;
    std::vector<std::string> library_roots;
    std::vector<std::string> craft_roots;
    // JSON queries containing each Craft's project and loaded script inputs.
    std::vector<std::string> crafts;
};

// Query execution supplies the actual loaded sources alongside the project's JSON.
auto format_project_query(std::string_view project, std::span<const std::string> scripts) noexcept
    -> std::string;

// The caller supplies its absolute workspace root and paths relative to it (or
// absolute external paths). Inputs and module identities are borrowed for this call.
// Resources identify the toolchain and participating Crafts through absolute paths.
// The manifest describes this generation; scheduling and cache validity belong
// to its consumer.
auto format_compilation_manifest(
    std::string_view workspace_root,
    const CompilationManifestResources& resources,
    std::span<const CompilationManifestInput> inputs,
    const GeneratedArtifactSet& artifacts,
    std::size_t runtime_test_count,
    bool cacheable,
    std::span<const std::string_view> owned_modules,
    std::span<const std::string_view> standard_modules,
    std::span<const std::string_view> external_modules
) noexcept -> std::string;
