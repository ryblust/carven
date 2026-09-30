module carven:test.internal.compiler.diagnostics.presentation;

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

struct Rejection final {
    SourceManager sources;
    Diagnostics diagnostics;
};

auto reject(std::string_view source) noexcept -> Rejection {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("app.cv", std::string(source));
    ct::require(source_id.has_value());
    const auto module_path = CanonicalModulePath::from_value("app");
    ct::require(module_path.has_value());
    const auto input = SourceModuleInput {
        .source_id = *source_id,
        .module_path = *module_path,
    };
    auto result = compile(
        sources,
        SourceBatch {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:presentation").value(),
        }
    );
    auto diagnostics = Diagnostics();
    if (ct::expect(!result.has_value())) {
        diagnostics = std::move(result.error());
    }
    return {.sources = std::move(sources), .diagnostics = std::move(diagnostics)};
}

auto primary_texts(const Rejection& rejection, DiagnosticCode code) noexcept
    -> std::vector<std::string> {
    auto texts = std::vector<std::string>();
    for (const auto& diagnostic : rejection.diagnostics) {
        if (diagnostic.finding.code == code && diagnostic.attachment.primary) {
            texts.emplace_back(rejection.sources.slice(diagnostic.attachment.primary->span));
        }
    }
    std::ranges::sort(texts);
    return texts;
}

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler diagnostics: independent bodies each report their first error",
        [] static noexcept {
            const auto rejection = reject(
                "fn first() -> i32 { return missing_first; } "
                "fn second() -> i32 { return missing_second; } "
                "test \"third\" { let value: i32 = missing_third; } "
                "const { let value = missing_fourth; }"
            );
            ct::expect(
                primary_texts(rejection, DiagnosticCode::NameUnresolved)
                == std::vector<std::string> {
                    "missing_first",
                    "missing_fourth",
                    "missing_second",
                    "missing_third",
                }
            );
        }
    );

    ct::test(
        "Compiler diagnostics: a body depending on a failed inferred contract stays silent",
        [] static noexcept {
            const auto rejection = reject(
                "private fn inferred() { return missing_source; } "
                "fn dependent() -> i32 { return inferred() + missing_dependent; } "
                "fn independent() -> i32 { return missing_independent; }"
            );
            ct::expect(
                primary_texts(rejection, DiagnosticCode::NameUnresolved)
                == std::vector<std::string> {"missing_independent", "missing_source"}
            );
            ct::expect_equal(rejection.diagnostics.size(), 2uz);
        }
    );

    ct::test("Compiler diagnostics: messages state the types they reject", [] static noexcept {
        struct Expectation final {
            std::string_view name;
            std::string_view source;
            DiagnosticCode code;
            std::string_view message;
        };

        static constexpr auto cases = std::to_array<Expectation>({
            {
                .name = "initializer",
                .source = "fn invalid() { let value: str = 1; }",
                .code = DiagnosticCode::TypeMismatch,
                .message = "expected type 'str', found 'i32'",
            },
            {
                .name = "aggregate and callable shapes",
                .source = "fn take(view: fn(i32) -> i32) {} "
                          "fn add(left: i32, right: i32) -> i32 { return left + right; } "
                          "fn invalid() { take(add); }",
                .code = DiagnosticCode::TypeMismatch,
                .message = "expected type 'fn(i32) -> i32', found 'fn(i32, i32) -> i32'",
            },
            {
                .name = "binary operands",
                .source = "fn invalid() { let value = \"text\" + 1; }",
                .code = DiagnosticCode::TypeBinary,
                .message = "binary operands have incompatible types: 'str' and 'i32'",
            },
            {
                .name = "condition",
                .source = "fn invalid() { if 3 {} }",
                .code = DiagnosticCode::TypeConditionBool,
                .message = "condition must have type bool, found 'i32'",
            },
            {
                .name = "missing return",
                .source = "fn invalid() -> [i32; 2] {}",
                .code = DiagnosticCode::FlowMissingReturn,
                .message = "reachable path of callable returning '[i32; 2]' has no return",
            },
        });
        ct::each(cases, &Expectation::name, [](const Expectation& item) static noexcept {
            const auto rejection = reject(item.source);
            const auto* diagnostic = ct::find_diagnostic(rejection.diagnostics, item.code);
            if (ct::expect(diagnostic != nullptr)) {
                ct::expect_equal(diagnostic->finding.message, item.message);
            }
        });
    });

    ct::test(
        "Compiler diagnostics: unresolved spellings suggest only a near candidate",
        [] static noexcept {
            struct Expectation final {
                std::string_view name;
                std::string_view source;
                DiagnosticCode code;
                std::string_view message;
            };

            static constexpr auto cases = std::to_array<Expectation>({
                {
                    .name = "local",
                    .source = "fn invalid() { let total = 1; let copy = totl; }",
                    .code = DiagnosticCode::NameUnresolved,
                    .message = "unresolved name 'totl'; did you mean 'total'?",
                },
                {
                    .name = "type",
                    .source = "struct Point { x: i32 } fn invalid(value: Piont) {}",
                    .code = DiagnosticCode::TypeUnresolved,
                    .message = "unresolved type name 'Piont'; did you mean 'Point'?",
                },
                {
                    .name = "field",
                    .source = "struct Point { width: i32 } "
                              "fn invalid(value: Point) -> i32 { return value.widht; }",
                    .code = DiagnosticCode::TypeMemberUnresolved,
                    .message =
                        "structure 'Point' has no field named 'widht'; did you mean 'width'?",
                },
                {
                    .name = "enum case",
                    .source = "enum Color { Red, Green } fn invalid() { let value = Color::Gren; }",
                    .code = DiagnosticCode::TypeMemberUnresolved,
                    .message = "enum 'Color' has no case named 'Gren'; did you mean 'Green'?",
                },
                {
                    .name = "unrelated name",
                    .source = "fn invalid() { let total = 1; let copy = zzzzzz; }",
                    .code = DiagnosticCode::NameUnresolved,
                    .message = "unresolved name 'zzzzzz'",
                },
                {
                    .name = "single character shares nothing",
                    .source = "struct Point { x: i32 } "
                              "fn invalid(value: Point) -> i32 { return value.z; }",
                    .code = DiagnosticCode::TypeMemberUnresolved,
                    .message = "structure 'Point' has no field named 'z'",
                },
            });
            ct::each(cases, &Expectation::name, [](const Expectation& item) static noexcept {
                const auto rejection = reject(item.source);
                const auto* diagnostic = ct::find_diagnostic(rejection.diagnostics, item.code);
                if (ct::expect(diagnostic != nullptr)) {
                    ct::expect_equal(diagnostic->finding.message, item.message);
                }
            });
        }
    );

    ct::test(
        "Compiler diagnostics: an unhandled test failure names its type and propagation",
        [] static noexcept {
            const auto rejection = reject(
                "struct E {} struct F {} "
                "fn source() throw E + F { throw E {}; } "
                "test \"unhandled\" { let before = 1; source()?; check(before == 1); source()?; }"
            );
            const auto* diagnostic =
                ct::find_diagnostic(rejection.diagnostics, DiagnosticCode::EffectRootUnhandled);
            if (!ct::expect(diagnostic != nullptr)) {
                return;
            }
            ct::expect_equal(
                diagnostic->finding.message,
                std::string_view("test leaves failures unhandled: app.E, app.F")
            );
            ct::expect(
                primary_texts(rejection, DiagnosticCode::EffectRootUnhandled)
                == std::vector<std::string> {"source()?"}
            );
            if (ct::expect_equal(diagnostic->attachment.related.size(), 1uz)) {
                ct::expect_equal(
                    rejection.sources.slice(diagnostic->attachment.related.front().span),
                    std::string_view("source()?")
                );
                ct::expect(
                    diagnostic->attachment.primary->span.span.start()
                    < diagnostic->attachment.related.front().span.span.start()
                );
            }
            ct::expect_equal(diagnostic->attachment.helps.size(), 1uz);
        }
    );

    ct::test(
        "Compiler diagnostics: a contract excess names the propagation in this body",
        [] static noexcept {
            const auto rejection = reject(
                "struct E {} struct F {} "
                "private fn inner() { source()?; } "
                "fn source() throw E + F { throw E {}; } "
                "fn outer() throw F { inner()?; }"
            );
            const auto* diagnostic =
                ct::find_diagnostic(rejection.diagnostics, DiagnosticCode::EffectSignatureBound);
            if (!ct::expect(diagnostic != nullptr)) {
                return;
            }
            ct::expect_equal(
                diagnostic->finding.message,
                std::string_view("callable body exceeds its declared failure contract: app.E")
            );
            ct::expect(
                primary_texts(rejection, DiagnosticCode::EffectSignatureBound)
                == std::vector<std::string> {"inner()?"}
            );
            if (ct::expect_equal(diagnostic->attachment.helps.size(), 1uz)) {
                ct::expect(diagnostic->attachment.helps.front().contains("add 'E' to"));
            }
        }
    );

    ct::test(
        "Compiler diagnostics: a read-only write names the binding declaration",
        [] static noexcept {
            struct Expectation final {
                std::string_view name;
                std::string_view source;
                std::string_view declaration;
                std::string_view help;
            };

            static constexpr auto cases = std::to_array<Expectation>({
                {
                    .name = "local",
                    .source = "fn invalid() { let value = 1; value = 2; }",
                    .declaration = "value",
                    .help = "'var'",
                },
                {
                    .name = "parameter",
                    .source = "struct Point { x: i32 } fn invalid(point: Point) { point.x = 2; }",
                    .declaration = "point",
                    .help = "'&'",
                },
            });
            ct::each(cases, &Expectation::name, [](const Expectation& item) static noexcept {
                const auto rejection = reject(item.source);
                const auto* diagnostic =
                    ct::find_diagnostic(rejection.diagnostics, DiagnosticCode::AccessImmutable);
                if (!ct::expect(diagnostic != nullptr)
                    || !ct::expect_equal(diagnostic->attachment.related.size(), 1uz)
                    || !ct::expect_equal(diagnostic->attachment.helps.size(), 1uz)) {
                    return;
                }
                ct::expect_equal(
                    rejection.sources.slice(diagnostic->attachment.related.front().span),
                    item.declaration
                );
                ct::expect(diagnostic->attachment.helps.front().contains(item.help));
            });
        }
    );

    ct::test("Compiler diagnostics: call arity names the declared function", [] static noexcept {
        const auto rejection = reject(
            "fn single(value: i32) -> i32 { return value; } "
            "fn invalid() -> i32 { return single(); }"
        );
        const auto* diagnostic =
            ct::find_diagnostic(rejection.diagnostics, DiagnosticCode::TypeCallArity);
        if (!ct::expect(diagnostic != nullptr)) {
            return;
        }
        ct::expect_equal(
            diagnostic->finding.message,
            std::string_view("call expects 1 argument but received 0")
        );
        if (ct::expect_equal(diagnostic->attachment.related.size(), 1uz)) {
            ct::expect_equal(
                rejection.sources.slice(diagnostic->attachment.related.front().span),
                std::string_view("single")
            );
        }
    });
});

} // namespace
