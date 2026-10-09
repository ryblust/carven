module carven:compiler.manifest;

import :artifacts;
import std;

struct CompilationManifestInput final {
    std::string_view path;
    std::string_view module;
};

// The caller supplies its absolute workspace root and paths relative to it (or
// absolute external paths). Inputs and module identities are borrowed for this call.
// The manifest describes this generation; scheduling and cache validity belong
// to its consumer.
auto format_compilation_manifest(
    std::string_view workspace_root,
    std::span<const CompilationManifestInput> inputs,
    const GeneratedArtifactSet& artifacts
) noexcept -> std::string;
