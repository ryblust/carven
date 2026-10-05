module carven:test.internal.compiler.diagnostics.staged_calls;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler: static arguments require an explicit constant source"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {
                .name = "let initialized by a literal",
                .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use() -> i32 { let index = 2; return lane(index); })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "index",
            },
            {
                .name = "var initialized by a literal",
                .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use() -> i32 { var index = 2; return lane(index); })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "index",
            },
            {
                .name = "ordinary parameter forwarded by a wrapper",
                .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use(index: i32) -> i32 => lane(index);)",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "index",
            },
            {
                .name = "const function ordinary parameter does not acquire static admission",
                .source = R"(const fn lane(const index: i32) -> i32 => index;
                const fn use(index: i32) -> i32 => lane(index);
                const result = use(2);)",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "index",
            },
            {
                .name = "let alias of a declared static parameter",
                .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use(const index: i32) -> i32 {
                    let alias = index;
                    return lane(alias);
                }
                fn caller() -> i32 => use(2);)",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "alias",
            },
            {
                .name = "let alias of an expanded iteration index",
                .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use() -> i32 {
                    var sum = 0;
                    const for index in 0..4 { let alias = index; sum += lane(alias); }
                    return sum;
                })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "alias",
            },
            {
                .name = "ordinary for over a constant range does not expand",
                .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use() -> i32 {
                    var sum = 0;
                    for index in 0..4 { sum += lane(index); }
                    return sum;
                })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "index",
            },
            {
                .name = "runtime range cannot supply static indices",
                .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use(count: i32) -> i32 {
                    var sum = 0;
                    for index in 0..count { sum += lane(index); }
                    return sum;
                })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "index",
            },
        });
        check_compiler_errors(cases);
    };

    "Compiler: static parameters cannot escape as runtime values or cross an open native ABI"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {
                    .name = "unbound static function stored as a runtime value",
                    .source = R"(fn lane(const index: i32) -> i32 => index;
                fn use() -> i32 { let selected = lane; return selected(2); })",
                    .code = DiagnosticCode::ConstAdmission,
                    .primary_text = "lane",
                },
                {
                    .name = "unbound imported native parameter",
                    .source = "import(cpp) fn lane(const index: i32) -> i32;",
                    .code = DiagnosticCode::ConstAdmission,
                    .primary_text = "const",
                },
            });
            check_compiler_errors(cases);
        };

    "Compiler: fixed body types and lambda definitions need closed static roots"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "a fixed array type cannot depend on an unbound static parameter",
             .source =
                 "fn use(const count: i32) { const extent = count; let values: [i32; extent] = [1, 2]; }",
             .code = DiagnosticCode::ConstArrayExtent,
             .primary_text = "extent"},
            {.name = "a type in an unselected arm reports the failure of the root it reads",
             .source =
                 "struct Broken {} const fn broken() -> i32 throw Broken { throw Broken {}; } "
                 "fn use() { const if false { const count = broken()?; let values: [i32; count] = []; } }",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = "throw Broken {};"},
            {.name = "a lambda body cannot import an unbound static local",
             .source =
                 "fn use(const count: i32) { const value = count + 1; let closure = []() -> i32 => value; }",
             .code = DiagnosticCode::ConstAdmission,
             .primary_text = "value"},
            {.name = "a static local is not runtime capture storage",
             .source = "fn use() { const value = 1; let closure = [value]() -> i32 => value; }",
             .code = DiagnosticCode::LambdaCaptureInvalid,
             .primary_text = "value"},
        });
        check_compiler_errors(cases);
    };

    "Compiler: a failed static body reports its error once to every requester"_test =
        [] static noexcept {
            const auto cases = std::to_array<std::string_view>({
                "const fn broken(const d: i32) -> i32 { const x = 1 / d; return x; } "
                "const test { broken(0); } const test { broken(0); }",
                "const fn zero() -> i32 => 0; "
                "const fn broken() -> i32 { const x = 1 / zero(); return x; } "
                "const a = broken(); const b = broken(); "
                "fn use() -> i32 { const c = broken(); return c; }",
                "fn broken(const d: i32) -> i32 { const x = 1 / d; return x; } "
                "fn a() -> i32 => broken(0); fn b() -> i32 => broken(0); "
                "const fn c() -> i32 => broken(0); const v = c();",
            });
            for (const auto source : cases) {
                with_compiled_source(source, [](const auto&, const auto& result) static noexcept {
                    require(!result.has_value());
                    expect_equal(result.error().size(), 1uz);
                    expect_diagnostic(result.error(), DiagnosticCode::ConstDivideByZero);
                });
            }
        };

    "Compiler: static control requires static inputs and integer ranges"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {
                .name = "const for over a runtime range",
                .source = R"(fn use(count: i32) { const for index in 0..count {} })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "0..count",
            },
            {
                .name = "const for over an array",
                .source = R"(fn use() { const for value in [1, 2] {} })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "const",
            },
            {
                .name = "a captured static parameter is runtime closure storage",
                .source = R"(fn choose(const enabled: bool) -> i32 {
                    let select = [enabled]() -> i32 {
                        const if enabled { return 1; } else { return 0; }
                    };
                    return select();
                }
                fn caller() -> i32 => choose(true);)",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "enabled",
            },
            {
                .name = "const if with a runtime condition",
                .source = R"(fn use(flag: bool) -> i32 { const if flag { return 1; } return 0; })",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "flag",
            },
            {
                .name = "const if else-if arm with a runtime condition",
                .source = R"(fn use(const enabled: bool, flag: bool) -> i32 {
                    const if enabled { return 1; } else if flag { return 2; }
                    return 0;
                }
                fn caller(flag: bool) -> i32 => use(true, flag);)",
                .code = DiagnosticCode::ConstAdmission,
                .primary_text = "flag",
            },
            {
                .name = "selected const if arm evaluates its static root",
                .source = R"(fn use(const divisor: i32) -> i32 {
                    const if divisor != 1 { const factor = 1 / divisor; return factor; }
                    return 1;
                }
                fn caller() -> i32 => use(0);)",
                .code = DiagnosticCode::ConstDivideByZero,
                .primary_text = "/",
            },
        });
        check_compiler_errors(cases);
    };

    "Compiler: inactive runtime paths still require local static roots"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {
                .name = "constant local in an unselected branch",
                .source = R"(fn use(runtime: i32) {
                if false { const required = runtime; }
            })",
                .code = DiagnosticCode::ConstInitializer,
                .primary_text = "const required = runtime",
            },
            {
                .name = "constant local after a selected return",
                .source = R"(fn use(runtime: i32, const selected: bool) {
                if selected { return; }
                const required = runtime;
            }
            fn caller() { use(3, true); })",
                .code = DiagnosticCode::ConstInitializer,
                .primary_text = "const required = runtime",
            },
            {
                .name = "constant local reads a runtime range index",
                .source = R"(fn use(count: i32) {
                for index in 0..count { if false { const required = index; } }
            })",
                .code = DiagnosticCode::ConstInitializer,
                .primary_text = "const required = index",
            },
        });
        check_compiler_errors(cases);
    };
});

} // namespace
