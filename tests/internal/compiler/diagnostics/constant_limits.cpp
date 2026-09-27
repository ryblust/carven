module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.constant_limits;

import :source.batch;
import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE(
    "Compiler diagnostics: constant resource limits retain source locations and bounded call traces"
) {
    auto retained = std::string("const values = [0");
    for (auto index = 1uz; index < 2048uz; ++index) {
        retained += ",0";
    }
    retained += "]; const retained = values.as_slice()";
    for (auto index = 0uz; index < 256uz; ++index) {
        retained += ".slice(0, 2048)";
    }
    retained += ';';
    {
        auto sources = SourceManager();
        const auto source_id = *sources.append_virtual("constant-view-limit.cv", retained);
        const auto input = SourceModuleInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("constant_view_limit"),
        };
        const auto result = compile(
            sources,
            SourceBatch {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:constant-view-limit").value(),
            }
        );
        CHECK(result.has_value());
    }

    auto copied = std::string("const fn exhaust() -> i32 { let values = [0");
    for (auto index = 1uz; index < 2048uz; ++index) {
        copied += ",0";
    }
    copied += "]; var sum = 0; for index in 0..260 { let copy = values; "
              "sum += copy[0]; } return sum; } const result = exhaust();";
    const auto cases = std::to_array<std::string_view>({
        copied,
        "const fn endless() -> i32 { while true {} return 0; } "
        "const result = endless();",
        "const fn recursive() -> i32 => recursive(); "
        "const result = recursive();",
        R"(const fn grow() -> String {
            var text: String = "abcdefghijklmnop";
            for index in 0..20 {
                let copy = String::from_str(text.as_str());
                text.append(copy.as_str());
            }
            return text;
        } const result = grow();)",
        R"(const fn grow() -> String {
            var text: String = "abcdefghijklmnop";
            for index in 0..20 {
                let copy = String::from_str(text.as_str());
                text.append_format(f"{copy}");
            }
            return text;
        } const result = grow();)",
    });
    for (const auto source : cases) {
        CAPTURE(source.substr(0, 80));
        auto sources = SourceManager();
        const auto source_id = *sources.append_virtual("constant-limit.cv", std::string(source));
        const auto input = SourceModuleInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("constant_limit"),
        };
        const auto result = compile(
            sources,
            SourceBatch {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:constant-limit").value(),
            }
        );
        REQUIRE_FALSE(result.has_value());
        if (result.has_value()) {
            continue;
        }
        const auto* diagnostic = find_compiler_diagnostic(result.error(), "CV-CONST-LIMIT");
        REQUIRE(diagnostic != nullptr);
        if (diagnostic == nullptr) {
            continue;
        }
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_FALSE(sources.slice(diagnostic->attachment.primary->span).empty());
        CHECK_LE(diagnostic->attachment.related.size(), 8uz);
    }
}
