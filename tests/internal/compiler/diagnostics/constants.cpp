module carven:test.internal.compiler.diagnostics.constants;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

struct ModuleFixture final {
    std::string_view origin;
    std::string_view path;
    std::string_view source;
};

struct VisibilityExpectation final {
    std::string_view name;
    std::string_view provider_path;
    std::string_view provider_source;
    std::string_view consumer_path;
    std::string_view consumer_source;
    bool succeeds;
};

auto compile_fixture(SourceManager& sources, std::span<const ModuleFixture> modules) noexcept
    -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics> {
    auto inputs = std::vector<SourceModuleInput>();
    inputs.reserve(modules.size());
    for (const auto& source_module : modules) {
        const auto source_id = sources.append_virtual(
            std::string(source_module.origin),
            std::string(source_module.source)
        );
        const auto module_path = CanonicalModulePath::from_value(source_module.path);
        require(source_id.has_value());
        require(module_path.has_value());
        inputs.push_back({
            .source_id = *source_id,
            .module_path = *module_path,
        });
    }
    return compile(
        sources,
        SourceBatch {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:constants").value(),
        }
    );
}

auto diagnostic_count(std::span<const Diagnostic> diagnostics, DiagnosticCode code) noexcept
    -> std::size_t {
    return static_cast<std::size_t>(
        std::ranges::count_if(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        })
    );
}

const TestSuite suite([] static noexcept {
    "Top-level constants: visibility follows declaration audiences"_test = [] static noexcept {
        static constexpr auto cases = std::to_array<VisibilityExpectation>({
            {
                .name = "private constant from a sibling module",
                .provider_path = "api",
                .provider_source = "private const shared = 42;\n",
                .consumer_path = "user",
                .consumer_source = "import api using shared;\n"
                                   "fn read() -> i32 { return shared; }\n",
                .succeeds = false,
            },
            {
                .name = "bare constant within one module domain",
                .provider_path = "crafts.alpha.api",
                .provider_source = "const shared = 42;\n",
                .consumer_path = "crafts.alpha.user",
                .consumer_source = "import api using shared;\n"
                                   "export fn read() -> i32 { return shared; }\n",
                .succeeds = true,
            },
            {
                .name = "bare constant across module domains",
                .provider_path = "crafts.alpha.api",
                .provider_source = "const shared = 42;\n",
                .consumer_path = "crafts.beta.user",
                .consumer_source = "import alpha::api using shared;\n"
                                   "export fn read() -> i32 { return shared; }\n",
                .succeeds = false,
            },
            {
                .name = "exported constant across module domains",
                .provider_path = "crafts.alpha.api",
                .provider_source = "export const shared: i32 = 42;\n",
                .consumer_path = "crafts.beta.user",
                .consumer_source = "import alpha::api using shared;\n"
                                   "export fn read() -> i32 { return shared; }\n",
                .succeeds = true,
            },
        });

        each(cases, &VisibilityExpectation::name, [&](const auto& expectation) noexcept {
            auto sources = SourceManager();
            const auto modules = std::to_array<ModuleFixture>({
                {
                    .origin = "provider.cv",
                    .path = expectation.provider_path,
                    .source = expectation.provider_source,
                },
                {
                    .origin = "consumer.cv",
                    .path = expectation.consumer_path,
                    .source = expectation.consumer_source,
                },
            });

            const auto result = compile_fixture(sources, modules);
            if (expectation.succeeds) {
                expect(result.has_value());
                return;
            }
            if (!(expect(!(result.has_value())))) {
                return;
            }
            const auto* diagnostic =
                find_diagnostic(result.error(), DiagnosticCode::ImportResolution);
            if (!(expect(diagnostic != nullptr))) {
                return;
            }
            if (!(expect(diagnostic->attachment.primary.has_value()))) {
                return;
            }
            expect_equal(
                sources.slice(diagnostic->attachment.primary->span),
                std::string_view("shared")
            );
        });
    };

    "Top-level constants: direct cycles emit one stable diagnostic"_test = [] static noexcept {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "direct.cv",
                .path = "direct",
                .source = "const direct =\n"
                          "    direct;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        if (!expect(!(result.has_value()))) {
            return;
        }
        expect_equal(diagnostic_count(result.error(), DiagnosticCode::ConstCycle), 1uz);
        const auto* diagnostic = find_diagnostic(result.error(), DiagnosticCode::ConstCycle);
        if (!expect(diagnostic != nullptr)) {
            return;
        }
        if (!expect(diagnostic->attachment.primary.has_value())) {
            return;
        }
        expect_equal(
            sources.slice(diagnostic->attachment.primary->span),
            std::string_view("direct")
        );
    };

    "Top-level constants: cross-module cycles retain related locations"_test = [] static noexcept {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "a.cv",
                .path = "a",
                .source = "import b using second;\n"
                          "const first = second;\n",
            },
            {
                .origin = "b.cv",
                .path = "b",
                .source = "import a using first;\n"
                          "const second = first;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        if (!expect(!(result.has_value()))) {
            return;
        }
        expect_equal(diagnostic_count(result.error(), DiagnosticCode::ConstCycle), 1uz);
        const auto* diagnostic = find_diagnostic(result.error(), DiagnosticCode::ConstCycle);
        if (!expect(diagnostic != nullptr)) {
            return;
        }
        if (!expect(diagnostic->attachment.primary.has_value())) {
            return;
        }
        expect_equal(diagnostic->attachment.related.size(), 2uz);
    };

    "Top-level constants: numeric enum case cycles emit one diagnostic"_test = [] static noexcept {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "enum-cycle.cv",
                .path = "enum_cycle",
                .source = "enum Code: i32 {\n"
                          "    First = Code::Second as i32,\n"
                          "    Second = Code::First as i32,\n"
                          "}\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        if (!expect(!(result.has_value()))) {
            return;
        }
        expect_equal(diagnostic_count(result.error(), DiagnosticCode::ConstCycle), 1uz);
    };

    "Top-level constants: enum owners resolve before case lookup"_test = [] static noexcept {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "enum-owner.cv",
                .path = "enum_owner",
                .source = "const invalid = Empty::Missing;\n"
                          "enum Empty {}\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        if (!expect(!(result.has_value()))) {
            return;
        }
        expect_equal(diagnostic_count(result.error(), DiagnosticCode::TypeEnumEmpty), 1uz);
        expect_equal(diagnostic_count(result.error(), DiagnosticCode::TypeMemberUnresolved), 0uz);
    };

    "Top-level constants: lexical facts stay outside declaration elaboration"_test =
        [] static noexcept {
            auto sources = SourceManager();
            static constexpr auto modules = std::to_array<ModuleFixture>({
                {
                    .origin = "lambda-initializer.cv",
                    .path = "lambda_initializer",
                    .source = "const invalid = [](value: i32) { return value; };\n",
                },
            });

            const auto result = compile_fixture(sources, modules);

            if (!expect(!(result.has_value()))) {
                return;
            }
            expect_equal(result.error().size(), 1uz);
            expect_equal(diagnostic_count(result.error(), DiagnosticCode::ConstInitializer), 1uz);
        };

    "Top-level constants: a nullary enum case remains a value"_test = [] static noexcept {
        static constexpr auto cases = std::to_array<std::string_view>({
            "enum Choice { Empty }\n"
            "const invalid = Choice::Empty();\n",
            "enum Choice { Empty, Value(i32) }\n"
            "const invalid = Choice::Empty();\n",
        });

        each(cases, std::identity {}, [&](const auto& source) noexcept {
            auto sources = SourceManager();
            const auto modules = std::to_array<ModuleFixture>({
                {
                    .origin = "nullary-call.cv",
                    .path = "nullary_call",
                    .source = source,
                },
            });
            const auto result = compile_fixture(sources, modules);

            if (!(expect(!(result.has_value())))) {
                return;
            }
            const auto* diagnostic =
                find_diagnostic(result.error(), DiagnosticCode::TypeEnumCaseArity);
            if (!(expect(diagnostic != nullptr))) {
                return;
            }
            if (!(expect(diagnostic->attachment.primary.has_value()))) {
                return;
            }
            expect_equal(
                sources.slice(diagnostic->attachment.primary->span),
                std::string_view("Choice::Empty()")
            );
        });
    };

    "Top-level constants: exported constants require a declaration type"_test = [] static noexcept {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "missing-type.cv",
                .path = "missing_type",
                .source = "export const answer = 42;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        if (!expect(!(result.has_value()))) {
            return;
        }
        const auto* diagnostic = find_diagnostic(result.error(), DiagnosticCode::ConstExportedType);
        if (!expect(diagnostic != nullptr)) {
            return;
        }
        if (!expect(diagnostic->attachment.primary.has_value())) {
            return;
        }
        expect_equal(
            sources.slice(diagnostic->attachment.primary->span),
            std::string_view("answer")
        );
    };

    "Top-level constants: exported constants reject private nominal identities"_test =
        [] static noexcept {
            auto sources = SourceManager();
            static constexpr auto modules = std::to_array<ModuleFixture>({
                {
                    .origin = "nominal-leak.cv",
                    .path = "nominal_leak",
                    .source = "private enum Hidden { Value }\n"
                              "export const exposed: Hidden = Hidden::Value;\n",
                },
            });

            const auto result = compile_fixture(sources, modules);

            if (!expect(!(result.has_value()))) {
                return;
            }
            const auto* diagnostic =
                find_diagnostic(result.error(), DiagnosticCode::TypeVisibilityLeak);
            if (!expect(diagnostic != nullptr)) {
                return;
            }
            expect(diagnostic->attachment.primary.has_value());
        };

    "Top-level constants: normalization removes private enum identity"_test = [] static noexcept {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "normalized-surface.cv",
                .path = "normalized_surface",
                .source = "private enum Hidden: i32 { Value = 7 }\n"
                          "export const exposed: i32 = Hidden::Value as i32;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        expect(result.has_value());
    };
});

} // namespace
