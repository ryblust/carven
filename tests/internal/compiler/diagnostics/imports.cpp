module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.imports;

import :artifacts;
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

TEST_CASE("Compiler diagnostics: unused imports are tracked per import declaration") {
    auto sources = SourceManager();
    const auto used_provider = *sources.append_virtual(
        "used-provider.cv",
        "export fn selected() -> i32 { return 1; }\n"
        "export fn selected_companion() -> i32 { return 3; }\n"
    );
    const auto unused_provider =
        *sources.append_virtual("unused-provider.cv", "export fn spare() -> i32 { return 2; }\n");
    const auto app = *sources.append_virtual(
        "app.cv",
        "import used_provider using { selected, selected_companion, };\n"
        "import unused_provider using spare;\n"
        "fn read() -> i32 { return selected(); }\n"
    );
    const auto inputs = std::array {
        CompilationInput {
            .source_id = used_provider,
            .module_path = *CanonicalModulePath::from_value("used_provider"),
        },
        CompilationInput {
            .source_id = unused_provider,
            .module_path = *CanonicalModulePath::from_value("unused_provider"),
        },
        CompilationInput {
            .source_id = app,
            .module_path = *CanonicalModulePath::from_value("app"),
        },
    };

    const auto result = compile(
        sources,
        CompilationRequest {.inputs = inputs},
        TargetGenerationRequest {
            .tests = TestEmissionMode::None,
            .linkage = ContentAddressedLinkageForm {},
        }
    );

    REQUIRE(result.has_value());
    const auto* unused = find_diagnostic(result->diagnostics, "CV-LINT-UNUSED-IMPORT");
    REQUIRE(unused != nullptr);
    REQUIRE(unused->attachment.primary.has_value());
    CHECK_EQ(unused->attachment.primary->span.source_id, app);
    CHECK_EQ(
        sources.slice(unused->attachment.primary->span),
        "import unused_provider using spare;"
    );
}

TEST_CASE("Compiler diagnostics: multiple wildcard providers remain ambiguous at use") {
    auto sources = SourceManager();
    const auto first =
        *sources.append_virtual("first.cv", "export fn value() -> i32 { return 1; }\n");
    const auto second =
        *sources.append_virtual("second.cv", "export fn value() -> i32 { return 2; }\n");
    const auto app = *sources.append_virtual(
        "app.cv",
        "import first using *;\n"
        "import second using *;\n"
        "const selected = value;\n"
    );
    const auto inputs = std::array {
        CompilationInput {
            .source_id = first,
            .module_path = *CanonicalModulePath::from_value("first")
        },
        CompilationInput {
            .source_id = second,
            .module_path = *CanonicalModulePath::from_value("second")
        },
        CompilationInput {.source_id = app, .module_path = *CanonicalModulePath::from_value("app")},
    };

    const auto result = compile(
        sources,
        CompilationRequest {.inputs = inputs},
        TargetGenerationRequest {
            .tests = TestEmissionMode::None,
            .linkage = ContentAddressedLinkageForm {},
        }
    );

    REQUIRE_FALSE(result.has_value());
    REQUIRE_EQ(result.error().size(), 1u);
    const auto* ambiguous = find_diagnostic(result.error(), "CV-NAME-AMBIGUOUS");
    REQUIRE(ambiguous != nullptr);
    REQUIRE(ambiguous->attachment.primary.has_value());
    CHECK_EQ(sources.slice(ambiguous->attachment.primary->span), "value");
    CHECK_EQ(ambiguous->attachment.related.size(), 2u);
}
