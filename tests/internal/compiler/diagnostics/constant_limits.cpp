module carven:test.internal.compiler.diagnostics.constant_limits;

import :diagnostics.code;
import :source.batch;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: constant resource limits retain source locations and bounded call traces"_test =
        [] static noexcept {
            const auto cases = std::to_array<std::string_view>({
                "const fn recursive() -> i32 => recursive(); "
                "const result = recursive();",
                "fn count(const index: i32) -> i32 { "
                "const if index == 127 { return 0; } return count(index + 1); } "
                "fn use() -> i32 => count(0);",
            });
            each(cases, std::identity {}, [&](const auto& source) noexcept {
                auto sources = SourceManager();
                const auto source_id =
                    *sources.append_virtual("constant-limit.cv", std::string(source));
                const auto input = SourceModuleInput {
                    .source_id = source_id,
                    .module_path = *CanonicalModulePath::from_value("constant_limit"),
                };
                const auto result = compile(
                    sources,
                    SourceBatch {.modules = std::span(&input, 1)},
                    TargetPlanningRequest {
                        .test_mode = TestGenerationMode::None,
                        .linkage_domain =
                            LinkageDomain::explicit_value("test:constant-limit").value(),
                        .library = std::nullopt,
                    }
                );
                expect(!(result.has_value())).note([&] noexcept {
                    return std::format("source.substr(0, 80): {}", source.substr(0, 80));
                });
                if (result.has_value()) {
                    return;
                }
                const auto* diagnostic =
                    find_diagnostic(result.error(), DiagnosticCode::ConstLimit);
                expect(diagnostic != nullptr).note([&] noexcept {
                    return std::format("source.substr(0, 80): {}", source.substr(0, 80));
                });
                if (diagnostic == nullptr) {
                    return;
                }
                expect(diagnostic->attachment.primary.has_value()).note([&] noexcept {
                    return std::format("source.substr(0, 80): {}", source.substr(0, 80));
                });
                if (!diagnostic->attachment.primary.has_value()) {
                    return;
                }
                expect(!(sources.slice(diagnostic->attachment.primary->span).empty()))
                    .note([&] noexcept {
                        return std::format("source.substr(0, 80): {}", source.substr(0, 80));
                    });
                expect_less_equal(diagnostic->attachment.related.size(), 8uz).note([&] noexcept {
                    return std::format("source.substr(0, 80): {}", source.substr(0, 80));
                });
            });
        };
});

} // namespace
