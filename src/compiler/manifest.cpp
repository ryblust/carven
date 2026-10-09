module carven:compiler.manifest.impl;

import :artifacts;
import :backend.generation.request;
import :compiler.manifest;
import :source.module_path;
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

auto test_mode_name(TestGenerationMode mode) noexcept -> std::string_view {
    switch (mode) {
        case TestGenerationMode::None:             return "none";
        case TestGenerationMode::RunnerHeader:     return "runner";
        case TestGenerationMode::RunnerEntryPoint: return "main";
    }
    std::unreachable();
}

auto domain_json(const LinkageDomain& domain) noexcept -> std::string {
    return std::format(
        "{{\"kind\": \"{}\", \"value\": {}}}",
        domain.kind() == LinkageDomainKind::Explicit ? "explicit" : "artifact-root",
        json_string(domain.value())
    );
}

auto library_json(const std::optional<SourceLibrary>& library) noexcept -> std::string {
    if (!library) {
        return "null";
    }
    auto result = std::format(
        "{{\"linkage_domain\": {}, \"modules\": [",
        domain_json(library->linkage_domain)
    );
    auto separator = std::string_view();
    for (const auto& module : library->modules) {
        result += separator;
        result += json_string(module.value());
        separator = ", ";
    }
    result += std::format("], \"emit_implementations\": {}}}", library->emit_implementations);
    return result;
}

} // namespace

auto format_compilation_manifest(
    std::string_view workspace_root,
    std::span<const CompilationManifestInput> inputs,
    const TargetPlanningRequest& request,
    const GeneratedArtifactSet& artifacts
) noexcept -> std::string {
    auto result = std::format(
        "{{\n  \"version\": 1,\n  \"workspace\": {{\"root\": {}}},\n"
        "  \"generation\": {{\"linkage_domain\": {}, \"tests\": \"{}\", \"library\": {}}},\n"
        "  \"inputs\": [",
        json_string(workspace_root),
        domain_json(request.linkage_domain),
        test_mode_name(request.test_mode),
        library_json(request.library)
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
            "{}\n    {{\"path\": {}, \"role\": \"{}\", \"source_mapping\": \"{}\"}}",
            separator,
            json_string(artifact.logical_path),
            role_name(artifact.role),
            mapping_name(artifact.source_mapping)
        );
        separator = ",";
    }
    result += "\n  ]\n}\n";
    return result;
}
