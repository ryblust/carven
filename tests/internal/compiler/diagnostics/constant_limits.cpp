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
                        .linkage_domain =
                            LinkageDomain::explicit_value("test:constant-view-limit").value(),
                    }
                );
                expect(result.has_value());
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
