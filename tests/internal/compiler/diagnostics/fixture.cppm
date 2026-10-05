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

struct CompilerErrorExpectation final {
    std::string_view name;
    std::string_view source;
    DiagnosticCode code;
    std::string_view primary_text;
};

template<typename Check>
auto with_compiled_source(std::string_view source, Check check) noexcept -> void {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("diagnostic.cv", std::string(source));
    require(source_id.has_value());
    const auto module_path = CanonicalModulePath::from_value("diagnostic");
    require(module_path.has_value());
    const auto input = SourceModuleInput {
        .source_id = *source_id,
        .module_path = *module_path,
    };
    check(
        sources,
        compile(
            sources,
            SourceBatch {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:semantics").value(),
            }
        )
    );
}

auto check_compiler_accepts(std::string_view source) noexcept -> void {
    with_compiled_source(source, [](const auto&, const auto& result) static noexcept {
        expect(result.has_value());
    });
}

auto check_compiler_error(
    std::string_view source,
    DiagnosticCode code,
    std::string_view primary_text
) noexcept -> void {
    with_compiled_source(source, [&](const auto& sources, const auto& result) noexcept {
        if (!expect(!result.has_value())) {
            return;
        }
        const auto* diagnostic = find_diagnostic(result.error(), code);
        expect_diagnostic(result.error(), code);
        if (diagnostic == nullptr) {
            return;
        }
        expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
        expect(diagnostic->attachment.primary.has_value());
        if (!diagnostic->attachment.primary.has_value()) {
            return;
        }
        expect_equal(sources.slice(diagnostic->attachment.primary->span), primary_text);
    });
}

auto check_compiler_errors(std::span<const CompilerErrorExpectation> cases) noexcept -> void {
    each(cases, &CompilerErrorExpectation::name, [](const auto& expectation) static noexcept {
        check_compiler_error(expectation.source, expectation.code, expectation.primary_text);
    });
}
