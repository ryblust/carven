module carven:test.internal.semantic.analysis.const_admission;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Constant function admission: capability diagnostics preserve evaluation order"_test = [] static noexcept {
        struct Scenario final {
            std::string_view name;
            std::string_view source;
            std::string_view primary;
            std::uint32_t start;
        };
        const auto scenarios = std::array {
            Scenario {
                .name = "left child is checked before right child",
                .source =
                    "fn first() -> i32 => 1; fn second() -> i32 => 2; const fn invalid() -> i32 => first() + second();\n",
                .primary = "first()",
                .start = 78u,
            },
            Scenario {
                .name = "earlier reachable statement is checked before later return",
                .source =
                    "fn first() -> i32 => 1; fn second() -> i32 => 2; const fn invalid() -> i32 { let value = first(); return second(); }\n",
                .primary = "first()",
                .start = 89u,
            },
            Scenario {
                .name = "parent call is checked before its argument",
                .source =
                    "fn ordinary(value: i32) -> i32 => value; const fn invalid() -> i32 => ordinary(ordinary(1));\n",
                .primary = "ordinary(ordinary(1))",
                .start = 70u,
            },
        };
        each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
            auto sources = SourceManager();
            const auto source = sources.append_virtual("analysis.cv", std::string(scenario.source));
            require(source.has_value());
            const auto input = SourceModuleInput {
                .source_id = *source,
                .module_path = semantic_test_module_path(),
            };
            auto parsed = parse_program(sources, SourceBatch {.modules = std::span(&input, 1uz)});
            require(parsed.has_value());
            const auto analyzed = analyze(std::move(*parsed));
            if (!expect(!analyzed.has_value())) {
                return;
            }
            expect_diagnostic(analyzed.error(), DiagnosticCode::ConstAdmission);
            const auto* diagnostic =
                find_diagnostic(analyzed.error(), DiagnosticCode::ConstAdmission);
            if (!expect(diagnostic != nullptr)) {
                return;
            }
            expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
            if (!expect(diagnostic->attachment.primary.has_value())) {
                return;
            }
            const auto primary = diagnostic->attachment.primary->span;
            if (!expect(primary.source_id == *source)) {
                return;
            }
            expect_equal(sources.slice(primary), scenario.primary);
            expect_equal(primary.span.start(), scenario.start);
        });
    };

    "Constant async admission: cold execution requires supported source capability"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                DiagnosticCode diagnostic;
            };
            const auto cases = std::array {
                Case {
                    .name = "unmarked source cold construction is not statically admitted",
                    .source = "async fn ordinary() -> i32 => 7; "
                              "const { let operation = ordinary(); }",
                    .diagnostic = DiagnosticCode::ConstAdmission
                },
                Case {
                    .name = "native cold construction requires unsupported provider capability",
                    .source = "private import(cpp) async fn provider() -> i32; "
                              "const { let operation = provider(); }",
                    .diagnostic = DiagnosticCode::ConstAdmission
                },
                Case {
                    .name = "an operation cannot be published as a frozen constant",
                    .source = "const async fn leaf() -> i32 => 7; const operation = leaf();",
                    .diagnostic = DiagnosticCode::ConstInitializer
                },
                Case {
                    .name = "recursive await shares the existing execution depth limit",
                    .source = "const async fn depth(value: i32) -> i32 { "
                              "if value == 0 { return 0; } return await depth(value - 1); } "
                              "const answer = await depth(256);",
                    .diagnostic = DiagnosticCode::ConstLimit
                },
            };
            each(cases, &Case::name, [](const Case& input) static noexcept {
                auto sources = SourceManager();
                const auto source =
                    sources.append_virtual("analysis.cv", std::string(input.source));
                const auto standard = sources.append_virtual("async.cv", "");
                require(source.has_value() && standard.has_value());
                const auto modules = std::array {
                    SourceModuleInput {
                        .source_id = *source,
                        .module_path = semantic_test_module_path(),
                    },
                    SourceModuleInput {
                        .source_id = *standard,
                        .module_path = *CanonicalModulePath::from_value("crafts.carven.std.async"),
                    },
                };
                auto parsed = parse_program(sources, SourceBatch {.modules = modules});
                if (!expect(parsed.has_value()).note("source = ", input.source)) {
                    return;
                }
                const auto analyzed = analyze(std::move(*parsed));
                if (!expect(!analyzed.has_value()).note("source = ", input.source)) {
                    return;
                }
                expect_diagnostic(analyzed.error(), input.diagnostic);
            });
        };

    "Constant async admission: static roots own cooperative source scheduling"_test = [] static noexcept {
        const auto cases = std::to_array<std::string_view>({
            "import std::async using yield_once; const async fn leaf() { await yield_once(); } const { await leaf(); }",
            "const async fn leaf() -> i32 => 7; const { async let child = leaf(); assert(await child == 7); }",
            "import std::async using { cancel, cancellation_point }; "
            "const async fn leaf() { await cancellation_point(); } "
            "const test \"close cancelled child\" { async let child = leaf(); cancel(child); }",
            "import std::async using cancellation_requested; const outside = cancellation_requested();",
        });
        each(cases, std::identity {}, [](std::string_view text) static noexcept {
            auto sources = SourceManager();
            const auto source = sources.append_virtual("analysis.cv", std::string(text));
            const auto standard = sources.append_virtual("async.cv", "");
            require(source.has_value() && standard.has_value());
            const auto modules = std::array {
                SourceModuleInput {
                    .source_id = *source,
                    .module_path = semantic_test_module_path()
                },
                SourceModuleInput {
                    .source_id = *standard,
                    .module_path = *CanonicalModulePath::from_value("crafts.carven.std.async")
                },
            };
            auto parsed = parse_program(sources, SourceBatch {.modules = modules});
            if (!expect(parsed.has_value()).note("source = ", text)) {
                return;
            }
            const auto analyzed = analyze(std::move(*parsed));
            expect(analyzed.has_value()).note("source = ", text);
        });
    };

    "Constant async cancellation: nominal catch cannot intercept a labelled root exit"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source = sources.append_virtual("analysis.cv", R"(
            import std::async using { cancel, cancellation_point };
            struct Failure {}
            const async fn leaf() throw Failure { await cancellation_point(); }
            const "cancelled root" {
                async let child = leaf();
                cancel(child);
                try { await child?; }
                catch { Failure(_) => {}, }
            }
        )");
            const auto standard = sources.append_virtual("async.cv", "");
            require(source.has_value() && standard.has_value());
            const auto modules = std::array {
                SourceModuleInput {
                    .source_id = *source,
                    .module_path = semantic_test_module_path()
                },
                SourceModuleInput {
                    .source_id = *standard,
                    .module_path = *CanonicalModulePath::from_value("crafts.carven.std.async")
                },
            };
            auto parsed = parse_program(sources, SourceBatch {.modules = modules});
            require(parsed.has_value());
            const auto analyzed = analyze(std::move(*parsed));
            if (!expect(!analyzed.has_value())) {
                return;
            }
            const auto* diagnostic =
                find_diagnostic(analyzed.error(), DiagnosticCode::ConstEvaluation);
            if (!expect(diagnostic != nullptr)) {
                return;
            }
            expect(diagnostic->finding.message.contains("cancellation"));
            expect(
                std::ranges::any_of(
                    diagnostic->attachment.related,
                    [](const auto& label) static noexcept {
                        return label.message.contains("cancelled root");
                    }
                )
            );
        };

    "Constant function admission: executable bodies are proved at their definitions"_test =
        [] static noexcept {
            const auto sources = std::to_array<std::string_view>({
                R"(const fn empty() {})",
                R"(const fn character(value: u32) -> char => char::from_u32_unchecked(value);)",
                R"(struct Failure {} const fn fail() throw Failure { throw Failure {}; })",
                R"(struct Failure {} private const fn fail() { throw Failure {}; })",
                R"(enum Value { One, Two }
            const fn select(value: Value) -> bool { return value == Value::One; })",
                R"(const fn output() { println(1); })",
                R"(const fn inactive() { if false { println(1); } })",
                R"(const fn report() { check(true); })",
                R"(struct Value { number: i32 }
            const fn field() -> i32 { let value = Value { number: 1 }; return value.number; })",
                R"(const fn integer(value: i32) -> i32 {
            return ((value + 2) * 3) ^ (~value);
        })",
                R"(const fn casts(value: i32) -> u32 {
            let present = (value != 0) as u32;
            return present + ('A' as u32);
        })",
                R"(const fn loop(limit: i32) -> i32 {
            var total = 0;
            var index = 0;
            while index < limit {
                index += 1;
                if index == 2 { continue; }
                total += index;
                if total > 100 { break; }
            }
            for var step = 0; step < limit; ++step { total += step; }
            for value in 0..limit { total += value; }
            return if total > 0 { total } else { 0 };
        })",
                R"(const fn choose(value: i32) -> i32 {
            return match value { 0 | 1 => 2, selected => selected, };
        })",
                R"(const fn text(input: str) -> String {
            let empty = String {};
            var result = String::from_str(input);
            if result.as_str().is_empty() { result.push('!'); }
            result.append("?");
            let copy = result;
            result.clear();
            return &&copy;
        })",
                R"(const fn format(value: i32) -> String { return f"value-{value:04}"; })",
                R"(const fn consume(&&value: String) -> usize { return value.len(); })",
                R"(const fn caller(value: i32) -> i32 { return callee(value); }
           const fn callee(value: i32) -> i32 { return value + 1; })",
                R"(const fn factorial(value: i32) -> i32 {
            return match value {
                ..2 => 1,
                _ => value * factorial(value - 1),
            };
        })",
            });
            each(sources, std::identity {}, [&](const auto& source) noexcept {
                const auto program = analyze_test_program(std::string(source));
                expect(program.declarations().functions().size() > 0uz);
            });
        };

    "Constant function admission: ordinary functions have no definition-time capability gate"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
            };

            const auto scenarios = std::to_array<Scenario>({
                {"inactive ordinary call", R"(fn ordinary() {}
            fn example() { if false { ordinary(); } })"},
                {"inactive short circuit call", R"(fn ordinary() -> bool => true;
            fn example() -> bool { return false && ordinary(); })"},
                {"Write parameter", R"(fn example(&value: i32) { value = 1; })"},
                {"pointer signature", R"(fn example(value: ptr<i32>) -> bool {
            return value == nullptr;
        })"},
                {"slice signature", R"(fn example(value: [i32]) -> usize { return value.len(); })"},
                {"array operation", R"(fn example() -> usize { return [1, 2].as_slice().len(); })"},
                {"closure value",
                 R"(fn example() -> i32 { let call = []() => 1; return call(); })"},
                {"unchecked scalar construction", R"(fn example() -> char {
            return char::from_u32_unchecked(65u32);
        })"},
                {"character range", R"(fn example(value: str) {
            for character in value.chars { let copy = character; }
        })"},
            });
            each(scenarios, &Scenario::name, [&](const auto& scenario) noexcept {
                const auto program = analyze_test_program(std::string(scenario.source));
                expect(program.declarations().functions().size() > 0uz);
            });
        };

    "Constant function admission: capability proof requires marked reachable callees"_test = [] static noexcept {
        const auto accepted = std::to_array<std::string_view>({
            "import(cpp) fn native(); const fn guaranteed() { return; native(); }",
            "import(cpp) fn native(); const fn guaranteed(flag: bool) { if flag { return; } else { return; } native(); }",
            "import(cpp) fn native(); const fn example(value: i32) { "
            "match value { _ => { return; }, } native(); }",
            "struct Failure {} fn ordinary() -> i32 => 1; "
            "const fn example(flag: bool) -> i32 throw Failure { "
            "return if flag { throw Failure {}; ordinary() } else { 2 }; }",
            "struct Failure {} fn ordinary(value: i32) -> i32 => value; "
            "const fn example() -> i32 throw Failure { "
            "return ordinary(if true { throw Failure {}; } else { throw Failure {}; }); }",
            "struct Failure {} fn ordinary() -> i32 => 1; "
            "const fn allowed(first: i32, second: i32) -> i32 => first + second; "
            "const fn example() -> i32 throw Failure { "
            "return allowed(if true { throw Failure {}; } else { throw Failure {}; }, ordinary()); }",
            "const fn guaranteed(&value: i32) { value = 1; }",
            "const fn guaranteed(value: ptr<i32>) -> bool => value == nullptr;",
        });
        each(accepted, std::identity {}, [&](const auto& source) noexcept {
            static_cast<void>(analyze_test_program(std::string(source)));
        });

        const auto rejected = std::to_array<std::string_view>({
            "fn ordinary() -> i32 => 42; const fn guaranteed() -> i32 => ordinary();",
            "fn leaf() -> i32 => 42; const fn middle() -> i32 => leaf(); "
            "const fn outer() -> i32 => middle();",
            "import(cpp) fn native(); const fn guaranteed(flag: bool) { if flag { native(); } }",
            "import(cpp) fn native(); const fn guaranteed() { if false { native(); } }",
            "fn ordinary() -> bool => true; const fn guaranteed() -> bool => false && ordinary();",
            "fn ordinary() -> i32 => 1; const fn example(flag: bool) -> i32 { "
            "return if flag { ordinary() } else { 2 }; }",
            "struct Failure {} fn ordinary() -> i32 => 1; "
            "const fn allowed(first: i32, second: i32) -> i32 => first + second; "
            "const fn example() -> i32 throw Failure { "
            "return allowed(ordinary(), if true { throw Failure {}; } else { throw Failure {}; }); }",
            "import(cpp) fn native(); fn ordinary() { native(); } const fn guaranteed() { ordinary(); }",
            "const fn guaranteed(action: fn() -> i32) -> i32 => action();",
            "const fn guaranteed() -> i32 { let action = []() => 1; return action(); }",
            "const fn guaranteed(value: str) { for character in value.chars {} }",
        });
        each(rejected, std::identity {}, [&](const auto& source) noexcept {
            const auto diagnostics = analyze_test_errors(std::string(source));
            expect_diagnostic(diagnostics, DiagnosticCode::ConstAdmission);
        });
    };

    "Constant functions: control dispatch does not construct an unreachable native result"_test =
        [] static noexcept {
            const auto accepted = std::to_array<std::string_view>({
                R"(struct Failure {}
            fn ordinary(value: ::Native) {}
            const fn example() throw Failure {
                ordinary(if true { throw Failure {}; } else { throw Failure {}; });
            })",
                R"(struct Failure {}
            fn ordinary(value: ::Native) {}
            const fn example(value: i32) throw Failure {
                ordinary(match value { _ => { throw Failure {}; }, });
            })",
                R"(struct Failure {}
            fn ordinary(value: ::Native) {}
            const fn example() throw Failure {
                ordinary(try { throw Failure {}; } catch { _ => { throw Failure {}; }, });
            })",
            });
            each(accepted, std::identity {}, [&](const auto& source) noexcept {
                static_cast<void>(analyze_test_program(std::string(source)));
            });

            const auto rejected = std::to_array<std::string_view>({
                R"(const fn example() { let value = if true { ::Native {} } else { ::Native {} }; })",
                R"(const fn example(value: i32) {
            let selected = match value { _ => ::Native {}, };
        })",
                R"(struct Failure {} const fn example() {
            let value = try { throw Failure {}; } catch { _ => ::Native {}, };
        })",
            });
            each(rejected, std::identity {}, [&](const auto& source) noexcept {
                const auto diagnostics = analyze_test_errors(std::string(source));
                expect_diagnostic(diagnostics, DiagnosticCode::ConstAdmission);
            });
        };

    "Constant functions: view signatures do not require executable target storage"_test =
        [] static noexcept {
            const auto accepted = std::to_array<std::string_view>({
                "const fn ignore(action: fn(::Native) -> ::Native) {}",
                "const fn length(values: [::Native]) -> usize => values.len();",
            });
            each(accepted, std::identity {}, [&](const auto& source) noexcept {
                static_cast<void>(analyze_test_program(std::string(source)));
            });
            const auto rejected = std::to_array<std::string_view>({
                "const fn own(value: ::Native) {}",
                "const fn own(values: [::Native; 2]) {}",
                "const fn invoke(action: fn(::Native) -> ::Native, value: ::Native) { action(value); }",
            });
            each(rejected, std::identity {}, [&](const auto& source) noexcept {
                const auto diagnostics = analyze_test_errors(std::string(source));
                expect_diagnostic(diagnostics, DiagnosticCode::ConstAdmission);
            });
        };

    "Constant function admission: required contexts call only explicit const functions"_test =
        [] static noexcept {
            const auto rejected = std::to_array<std::string_view>({
                "fn ordinary() -> i32 => 1 + 2; const answer = ordinary();",
                "fn ordinary() -> usize => 2; fn use(value: [i32; ordinary()]) {}",
                "fn ordinary() -> i32 => 42; const { let answer = ordinary(); }",
                "fn ordinary() -> i32 => 42; const test \"ordinary\" { check(ordinary() == 42); }",
                "fn ordinary() -> i32 => 42; const test \"named value\" { "
                "let selected = ordinary; check(selected() == 42); }",
            });
            each(rejected, std::identity {}, [&](const auto& source) noexcept {
                const auto diagnostics = analyze_test_errors(std::string(source));
                expect_diagnostic(diagnostics, DiagnosticCode::ConstAdmission);
            });

            const auto program = analyze_test_program(R"(
        const fn value() -> i32 => 42;
        const answer = value();
        fn ordinary() -> i32 => value();
        const test "named const value" { let selected = value; check(selected() == 42); }
    )");
            expect(program.declarations().functions().size() == 2uz);
        };
});

} // namespace
