module carven:compiler.manifest.impl;

import :artifacts;
import :compiler.manifest;
import :support.path;
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
        case GeneratedArtifactRole::SemanticIR:           return "semantic-ir";
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

auto source_reference(std::string_view source, std::string_view workspace_root) noexcept
    -> std::string {
    const auto path = path_from_utf8(source);
    if (path.is_absolute()) {
        const auto relative = path.lexically_relative(path_from_utf8(workspace_root));
        if (!relative.empty() && *relative.begin() != "..") {
            return json_string(path_to_generic_utf8(relative));
        }
    }
    return json_string(source);
}

auto json_strings(std::span<const std::string> values) noexcept -> std::string {
    auto result = std::string("[");
    auto separator = std::string_view();
    for (const auto& value : values) {
        result += separator;
        result += json_string(value);
        separator = ", ";
    }
    result += ']';
    return result;
}

} // namespace

auto format_project_query(std::string_view project, std::span<const std::string> scripts) noexcept
    -> std::string {
    return std::format(
        "{{\n  \"scripts\": {},\n  \"project\": {}\n}}\n",
        json_strings(scripts),
        project
    );
}

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
) noexcept -> std::string {
    auto result = std::format(
        "{{\n  \"workspace\": {{\"root\": {}}},\n"
        "  \"cacheable\": {},\n"
        "  \"runtime_test_count\": {},\n"
        "  \"include_dirs\": {},\n"
        "  \"library_roots\": {},\n"
        "  \"craft_roots\": {},\n"
        "  \"crafts\": [{}],\n"
        "  \"inputs\": [",
        json_string(workspace_root),
        cacheable,
        runtime_test_count,
        json_strings(resources.include_dirs),
        json_strings(resources.library_roots),
        json_strings(resources.craft_roots),
        std::ranges::fold_left(
            resources.crafts,
            std::string(),
            [](std::string result, const auto& craft) noexcept {
                if (!result.empty()) {
                    result += ",\n";
                }
                result += craft;
                return result;
            }
        )
    );
    auto separator = std::string_view();
    for (const auto& input : inputs) {
        result += std::format(
            "{}\n    {{\"path\": {}, \"module\": {}, \"owned\": {}, \"standard\": {}, "
            "\"external\": {}}}",
            separator,
            json_string(input.path),
            input.module_path.empty() ? std::string("null") : json_string(input.module_path),
            input.module_path.empty() || std::ranges::contains(owned_modules, input.module_path),
            std::ranges::contains(standard_modules, input.module_path),
            std::ranges::contains(external_modules, input.module_path)
        );
        separator = ",";
    }
    result += "\n  ],\n  \"artifacts\": [";
    separator = {};
    for (const auto& artifact : artifacts.entries()) {
        result += std::format(
            "{}\n    {{\"path\": {}, \"role\": \"{}\", \"source_mapping\": \"{}\", "
            "\"source\": {}, \"link_dependencies\": {}}}",
            separator,
            json_string(artifact.logical_path),
            role_name(artifact.role),
            mapping_name(artifact.source_mapping),
            artifact.source_path ? source_reference(*artifact.source_path, workspace_root)
                                 : std::string("null"),
            json_strings(artifact.link_dependencies)
        );
        separator = ",";
    }
    result += "\n  ]\n}\n";
    return result;
}
