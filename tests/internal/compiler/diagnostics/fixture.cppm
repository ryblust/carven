module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.fixture;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

auto find_compiler_diagnostic(
    std::span<const Diagnostic> diagnostics,
    std::string_view code
) noexcept -> const Diagnostic* {
    const auto found =
        std::ranges::find_if(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        });
    return found == diagnostics.end() ? nullptr : &*found;
}

struct NonemptyPrimarySpan final {};

struct CompilerErrorExpectation final {
    std::string_view name;
    std::string_view source;
    std::string_view code;
    std::variant<std::string_view, NonemptyPrimarySpan> primary_text;
};

auto check_compiler_error(
    std::string_view source,
    std::string_view code,
    std::variant<std::string_view, NonemptyPrimarySpan> primary_text
) noexcept -> void {
    auto sources = SourceManager();
    const auto source_id = *sources.append_virtual("diagnostic.cv", std::string(source));
    const auto input = SourceModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("diagnostic"),
    };

    const auto result = compile(
        sources,
        SourceBatch {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:semantics").value(),
        }
    );

    CHECK(!result.has_value());
    if (result.has_value()) {
        return;
    }
    const auto* diagnostic = find_compiler_diagnostic(result.error(), code);
    CHECK(diagnostic != nullptr);
    if (diagnostic == nullptr) {
        return;
    }
    CHECK_EQ(diagnostic->finding.severity, DiagnosticSeverity::Error);
    CHECK(diagnostic->attachment.primary.has_value());
    if (!diagnostic->attachment.primary.has_value()) {
        return;
    }
    if (const auto* text = std::get_if<std::string_view>(&primary_text)) {
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), *text);
    } else {
        CHECK(!diagnostic->attachment.primary->span.span.empty());
    }
}

auto check_compiler_errors(std::span<const CompilerErrorExpectation> cases) noexcept -> void {
    for (const auto& expectation : cases) {
        CAPTURE(expectation.name);
        check_compiler_error(expectation.source, expectation.code, expectation.primary_text);
    }
}
