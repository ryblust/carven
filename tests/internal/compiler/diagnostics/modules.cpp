module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.modules;

import :artifacts;
import :backend.generation.request;
import :compilation.request;
import :compiler.compile;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

auto find_diagnostic(std::span<const Diagnostic> diagnostics, std::string_view code) noexcept
    -> const Diagnostic* {
    const auto found =
        std::ranges::find_if(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        });
    return found == diagnostics.end() ? nullptr : &*found;
}

struct ErrorExpectation final {
    std::string_view name;
    std::string_view source;
    std::string_view code;
    std::string_view primary_text;
};

} // namespace

TEST_CASE("Compiler diagnostics: module-scoped facts retain their owning source") {
    auto sources = SourceManager();
    const auto healthy_source = *sources.append_virtual("healthy.cv", "fn healthy() {}\n");
    static constexpr auto failing_text = std::string_view(
        "struct Failure {}\n"
        "private fn caller() -> i32 { return failing(); }\n"
        "private fn failing() -> i32 { throw Failure {}; }\n"
    );
    const auto failing_source = *sources.append_virtual("failing.cv", std::string(failing_text));
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = healthy_source,
            .module_path = *CanonicalModulePath::from_value("healthy"),
        },
        CompilationModuleInput {
            .source_id = failing_source,
            .module_path = *CanonicalModulePath::from_value("failing"),
        },
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = inputs},
        TargetGenerationRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
        }
    );

    REQUIRE(!result.has_value());
    const auto* diagnostic = find_diagnostic(result.error(), "CV-EFFECT-UNMARKED");
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->attachment.primary.has_value());
    CHECK_EQ(diagnostic->attachment.primary->span.source_id, failing_source);
    CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "failing()");
}

TEST_CASE("Compiler diagnostics: module graph errors use one catalog identity space") {
    SUBCASE("explicit imports cannot bind two symbols to one name") {
        auto sources = SourceManager();
        const auto first =
            *sources.append_virtual("first.cv", "export fn value() -> i32 { return 1; }\n");
        const auto second =
            *sources.append_virtual("second.cv", "export fn value() -> i32 { return 2; }\n");
        const auto app = *sources.append_virtual(
            "app.cv",
            "import first using value;\n"
            "import second using value;\n"
            "fn read() -> i32 { return value(); }\n"
        );
        const auto inputs = std::array {
            CompilationModuleInput {
                .source_id = first,
                .module_path = *CanonicalModulePath::from_value("first")
            },
            CompilationModuleInput {
                .source_id = second,
                .module_path = *CanonicalModulePath::from_value("second")
            },
            CompilationModuleInput {
                .source_id = app,
                .module_path = *CanonicalModulePath::from_value("app")
            },
        };

        const auto result = compile(
            sources,
            CompilationRequest {.modules = inputs},
            TargetGenerationRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
            }
        );

        REQUIRE(!result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), "CV-IMPORT-RESOLUTION");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(diagnostic->attachment.primary->span.source_id, app);
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "value");
    }

    SUBCASE("entry-point uniqueness spans source modules") {
        auto sources = SourceManager();
        const auto first = *sources.append_virtual("first.cv", "fn main() {}\n");
        const auto second = *sources.append_virtual("second.cv", "fn main() {}\n");
        const auto inputs = std::array {
            CompilationModuleInput {
                .source_id = first,
                .module_path = *CanonicalModulePath::from_value("first")
            },
            CompilationModuleInput {
                .source_id = second,
                .module_path = *CanonicalModulePath::from_value("second")
            },
        };

        const auto result = compile(
            sources,
            CompilationRequest {.modules = inputs},
            TargetGenerationRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
            }
        );

        REQUIRE(!result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), "CV-ENTRY-DUPLICATE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        REQUIRE_EQ(diagnostic->attachment.related.size(), 1u);
        CHECK_EQ(diagnostic->attachment.primary->span.source_id, second);
        CHECK_EQ(diagnostic->attachment.related.front().span.source_id, first);
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "main");
    }

    SUBCASE("recursive storage across modules is diagnosed semantically") {
        auto sources = SourceManager();
        const auto first =
            *sources.append_virtual("a.cv", "import b using B;\nexport struct A { value: B }\n");
        const auto second =
            *sources.append_virtual("b.cv", "import a using A;\nexport struct B { value: A }\n");
        const auto inputs = std::array {
            CompilationModuleInput {
                .source_id = first,
                .module_path = *CanonicalModulePath::from_value("a")
            },
            CompilationModuleInput {
                .source_id = second,
                .module_path = *CanonicalModulePath::from_value("b")
            },
        };

        const auto result = compile(
            sources,
            CompilationRequest {.modules = inputs},
            TargetGenerationRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
            }
        );

        REQUIRE(!result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), "CV-TYPE-RECURSIVE-STORAGE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK(
            (diagnostic->attachment.primary->span.source_id == first
             || diagnostic->attachment.primary->span.source_id == second)
        );
    }
}
