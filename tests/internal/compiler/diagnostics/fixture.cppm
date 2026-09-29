module carven:test.internal.compiler.diagnostics.fixture;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

} // namespace

struct NonemptyPrimarySpan final {};

struct CompilerErrorExpectation final {
    std::string_view name;
    std::string_view source;
    DiagnosticCode code;
    std::variant<std::string_view, NonemptyPrimarySpan> primary_text;
};

auto check_compiler_error(
    std::string_view source,
    DiagnosticCode code,
    std::variant<std::string_view, NonemptyPrimarySpan> primary_text
) noexcept -> void {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("diagnostic.cv", std::string(source));
    ct::require(source_id.has_value());
    const auto module_path = CanonicalModulePath::from_value("diagnostic");
    ct::require(module_path.has_value());
    const auto input = SourceModuleInput {
        .source_id = *source_id,
        .module_path = *module_path,
    };

    const auto result = compile(
        sources,
        SourceBatch {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:semantics").value(),
        }
    );

    if (!ct::expect(!result.has_value())) {
        return;
    }
    const auto* diagnostic = ct::find_diagnostic(result.error(), code);
    ct::expect_diagnostic(result.error(), code);
    if (diagnostic == nullptr) {
        return;
    }
    ct::expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
    ct::expect(diagnostic->attachment.primary.has_value());
    if (!diagnostic->attachment.primary.has_value()) {
        return;
    }
    if (const auto* text = std::get_if<std::string_view>(&primary_text)) {
        ct::expect_equal(sources.slice(diagnostic->attachment.primary->span), *text);
    } else {
        ct::expect(!diagnostic->attachment.primary->span.span.empty());
    }
}

auto check_compiler_errors(std::span<const CompilerErrorExpectation> cases) noexcept -> void {
    ct::each(cases, &CompilerErrorExpectation::name, [](const auto& expectation) static noexcept {
        check_compiler_error(expectation.source, expectation.code, expectation.primary_text);
    });
}
