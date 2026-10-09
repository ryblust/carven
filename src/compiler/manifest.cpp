module carven:compiler.manifest.impl;

import :artifacts;
import :compiler.manifest;
import std;

namespace {

auto json_string(std::string_view value) noexcept -> std::string {
    auto result = std::string("\"");
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            result.push_back('\\');
            result.push_back(character);
        } else if (byte < 0x20u) {
            result += std::format("\\u{:04x}", byte);
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

auto role_name(GeneratedArtifactRole role) noexcept -> std::string_view {
    switch (role) {
        case GeneratedArtifactRole::Interface:            return "interface";
        case GeneratedArtifactRole::CppAPIHeader:         return "cpp-api-header";
        case GeneratedArtifactRole::ModuleImplementation: return "module-implementation";
        case GeneratedArtifactRole::TestRunnerHeader:     return "test-runner-header";
        case GeneratedArtifactRole::TestEntry:            return "test-entry";
    }
    std::unreachable();
}

auto mapping_name(ArtifactSourceMappingPolicy policy) noexcept -> std::string_view {
    switch (policy) {
        case ArtifactSourceMappingPolicy::StableInterface:  return "stable-interface";
        case ArtifactSourceMappingPolicy::SourceAttributed: return "source-attributed";
    }
    std::unreachable();
}

} // namespace

auto format_compilation_manifest(
    std::string_view workspace_root,
    std::span<const CompilationManifestInput> inputs,
    const GeneratedArtifactSet& artifacts
) noexcept -> std::string {
    auto result = std::format(
        "{{\n  \"workspace\": {{\"root\": {}}},\n"
        "  \"inputs\": [",
        json_string(workspace_root)
    );
    auto separator = std::string_view();
    for (const auto& input : inputs) {
        result += std::format(
            "{}\n    {{\"path\": {}, \"module\": {}}}",
            separator,
            json_string(input.path),
            json_string(input.module)
        );
        separator = ",";
    }
    result += "\n  ],\n  \"artifacts\": [";
    separator = {};
    for (const auto& artifact : artifacts.entries()) {
        result += std::format(
            "{}\n    {{\"path\": {}, \"role\": \"{}\", \"source_mapping\": \"{}\", \"source\": {}}}",
            separator,
            json_string(artifact.logical_path),
            role_name(artifact.role),
            mapping_name(artifact.source_mapping),
            artifact.source_path ? json_string(*artifact.source_path) : std::string("null")
        );
        separator = ",";
    }
    result += "\n  ]\n}\n";
    return result;
}
