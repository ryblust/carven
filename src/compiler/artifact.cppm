module carven:compiler.artifact;

import :semantic.analysis.program.checked;
import :support.binary;
import std;

// Root selection and external implementation ownership follow provenance ordinals.
// Module identities and source paths remain owned by semantic provenance.
struct CompilationModuleOwnership final {
    bool owned;
    bool standard;
    bool external;
};

// Section borrows remain valid while the encoded artifact storage is alive.
// Consumers restore published programs; source dependencies restore definitions.
struct CompilationArtifact final {
    std::vector<CompilationModuleOwnership> modules;
    bool cacheable;
    std::span<const std::byte> program;
    std::span<const std::byte> definitions;
};

auto encode_compilation_artifact(
    const CheckedUnit& unit,
    std::span<const CompilationModuleOwnership> modules
) noexcept -> std::expected<std::vector<std::byte>, BinaryError>;

auto read_compilation_artifact(std::span<const std::byte> bytes) noexcept
    -> std::expected<CompilationArtifact, BinaryError>;
