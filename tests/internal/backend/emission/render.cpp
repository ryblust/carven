module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.emission.render;

import :artifacts;
import :backend.emit;
import :backend.emission.render.string;
import :backend.target.builder;
import :backend.target.unit;
import std;

TEST_CASE("Emission: C++ string quoting owns escape syntax") {
    CHECK_EQ(cpp_string_token("a\\b\n\"c\t"), "\"a\\\\b\\012\\\"c\\011\"");
    CHECK_EQ(cpp_string_token(std::string_view("\0018\377", 3)), "\"\\0018\\377\"");
}

TEST_CASE("Emission: unit metadata is serialized without role-derived directives") {
    auto builder = TargetUnitBuilder();
    auto unit = std::move(builder).finish({
        .logical_path = "custom.cpp",
        .role = GeneratedArtifactRole::ModuleImplementation,
        .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
        .directive_groups = {{
            .directives = {{.bytes = "#custom first"}, {.bytes = "#custom second"}},
        }},
        .sections = {.preamble = {}, .body = {}, .epilogue = {}},
    });

    const auto artifact = emit(std::move(unit));
    CHECK_EQ(artifact.logical_path, "custom.cpp");
    CHECK_EQ(artifact.role, GeneratedArtifactRole::ModuleImplementation);
    CHECK_EQ(artifact.source_mapping, ArtifactSourceMappingPolicy::SourceAttributed);
    CHECK(artifact.content.contains("#custom first\n#custom second"));
    CHECK_FALSE(artifact.content.contains("carven/runtime"));
}
