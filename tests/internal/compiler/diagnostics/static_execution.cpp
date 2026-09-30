module carven:test.internal.compiler.diagnostics.static_execution;

import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :semantic.evaluation.output;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

auto compile_constant_program(
    std::string source,
    std::string& output,
    std::string& errors
) noexcept {
    auto sources = SourceManager();
    const auto id = *sources.append_virtual("static_execution.cv", std::move(source));
    const auto input = SourceModuleInput {
        .source_id = id,
        .module_path = *CanonicalModulePath::from_value("static_execution")
    };
    return compile(
        sources,
        SourceBatch {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("static-execution")
        },
        [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            (stream == ExecutionOutputStream::Standard ? output : errors) += bytes;
        }
    );
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler: static blocks execute local values and controls in source order",
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view prefix;
                std::string_view suffix;
            };
            const auto scenarios = std::array {
                Scenario {.name = "module block", .prefix = "const {", .suffix = "}"},
                Scenario {.name = "static test", .prefix = "const test {", .suffix = "}"},
                Scenario {
                    .name = "function block",
                    .prefix = "fn use() { const {",
                    .suffix = "} }"
                },
                Scenario {
                    .name = "instance block",
                    .prefix = "fn use(const selected: bool) { const if selected { const {",
                    .suffix = "} } } fn caller() { use(true); use(true); }",
                },
            };
            ct::each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
                auto output = std::string();
                auto errors = std::string();
                const auto result = compile_constant_program(
                    std::string(R"(
                    const fn observe(value: i32) -> i32 { print(f"v{value}"); return value; }
                    const fn lane(const index: i32) -> i32 {
                        const { print(f"i{index}"); }
                        return index;
                    }
                )") + std::string(scenario.prefix)
                        + R"(
                    print("a");
                    let first = observe(2);
                    const second = observe(first + 1);
                    const "nested" { assert(lane(first) == 2); }
                    const if second == 3 { print("b"); } else { print("wrong"); }
                    const for index in 0..2 {
                        const value = observe(index);
                        assert(lane(value) == index);
                        assert(lane(value) == index);
                    }
                    print("z");
                )" + std::string(scenario.suffix),
                    output,
                    errors
                );
                if (!ct::expect(result.has_value())) {
                    if (!result) {
                        for (const auto& diagnostic : result.error()) {
                            ct::expect(result.has_value()).note(diagnostic.finding.message);
                        }
                    }
                    return;
                }
                ct::expect_equal(output, std::string("av2v3i2bv0i0v1i1z"));
                ct::expect(errors.empty());
            });
        }
    );

    ct::test(
        "Compiler: selected static statement transfers stop later static roots",
        [] static noexcept {
            const auto sources = std::array<std::string_view, 3uz> {
                R"(
                    fn stop(flag: bool, const selected: bool, const divisor: i32) {
                        if flag {
                            const if selected { return; }
                        } else {
                            const if selected { return; }
                        }
                        const _ = 1 / divisor;
                    }
                    fn caller(flag: bool) { stop(flag, true, 0); }
                )",
                R"(
                    fn stop(const selected: bool, const divisor: i32) {
                        const for index in 0..3 {
                            const if selected { break; }
                            const _ = 1 / divisor;
                        }
                    }
                    fn caller() { stop(true, 0); }
                )",
                R"(
                    fn stop(const selected: bool, const divisor: i32) {
                        const for index in 0..3 {
                            const if selected { continue; }
                            const _ = 1 / divisor;
                        }
                    }
                    fn caller() { stop(true, 0); }
                )",
            };
            for (const auto source : sources) {
                auto output = std::string();
                auto errors = std::string();
                const auto result = compile_constant_program(std::string(source), output, errors);
                auto report = std::string();
                if (!result) {
                    for (const auto& diagnostic : result.error()) {
                        report += diagnostic.finding.message + "\n";
                    }
                }
                ct::expect(result.has_value()).note(source, "\n", report);
                ct::expect(output.empty());
                ct::expect(errors.empty());
            }
        }
    );
    ct::test("Compiler: loop step entry controls static roots", [] static noexcept {
        struct Scenario final {
            std::string_view name;
            std::string_view body;
            bool enters_step;
        };
        const auto scenarios = std::array {
            Scenario {
                .name = "break leaves the step's static root unentered",
                .body = "break;",
                .enters_step = false,
            },
            Scenario {
                .name = "continue requires the step's static root",
                .body = "continue;",
                .enters_step = true,
            },
        };
        ct::each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                std::string(R"(
                        fn use(const divisor: i32) {
                            for ; true; (if true { const _ = 1 / divisor; 0 } else { 0 }) {
                    )")
                    + std::string(scenario.body) + R"(
                            }
                        }
                        fn caller() { use(0); }
                    )",
                output,
                errors
            );
            if (scenario.enters_step) {
                if (ct::expect(!result.has_value())) {
                    ct::expect_diagnostic(result.error(), DiagnosticCode::ConstDivideByZero);
                }
            } else {
                auto findings = std::string();
                if (!result) {
                    for (const auto& diagnostic : result.error()) {
                        findings += diagnostic.finding.message + "\n";
                    }
                }
                ct::expect(result.has_value()).note(findings);
            }
            ct::expect(output.empty());
            ct::expect(errors.empty());
        });
    });
    ct::test(
        "Compiler: pattern completion follows predicate short circuits and failure boundaries",
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
                bool completes;
            };
            const auto scenarios = std::to_array<Scenario>({
                {
                    .name = "a stopped range bound leaves the whole match",
                    .source = R"(
                        fn use(value: i32, const divisor: i32) {
                            match value {
                                (if value == 0 { fail(); 0 } else { fail(); 0 }).. => {},
                                _ => {},
                            }
                            const _ = 1 / divisor;
                        }
                        fn caller(value: i32) { use(value, 0); }
                    )",
                    .completes = false,
                },
                {
                    .name = "an accepted or alternative skips a later stopped bound",
                    .source = R"(
                        fn use(value: i32, const divisor: i32) {
                            match value {
                                0 | (if value == 0 { fail(); 1 } else { fail(); 1 }).. => {},
                                _ => {},
                            }
                            const _ = 1 / divisor;
                        }
                        fn caller(value: i32) { use(value, 0); }
                    )",
                    .completes = true,
                },
                {
                    .name = "a sole enum tag reaches its stopped payload bound",
                    .source = R"(
                        enum Value { Only(i32) }
                        fn use(value: Value, flag: bool, const divisor: i32) {
                            match value {
                                .Only((if flag { fail(); 0 } else { fail(); 0 })..) => {},
                                _ => {},
                            }
                            const _ = 1 / divisor;
                        }
                        fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                    )",
                    .completes = false,
                },
                {
                    .name = "another enum tag skips the stopped payload bound",
                    .source = R"(
                        enum Value { Number(i32), Empty }
                        fn use(value: Value, flag: bool, const divisor: i32) {
                            match value {
                                .Number((if flag { fail(); 0 } else { fail(); 0 })..) => {},
                                _ => {},
                            }
                            const _ = 1 / divisor;
                        }
                        fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                    )",
                    .completes = true,
                },
                {
                    .name = "a stopped catch bound cannot enter the next handler",
                    .source = R"(
                        enum Failure { Only(i32) }
                        fn use(flag: bool, const divisor: i32) {
                            try { throw Failure::Only(0); } catch {
                                Failure(.Only((if flag { fail(); 0 } else { fail(); 0 })..)) => {},
                                _ => {},
                            }
                            const _ = 1 / divisor;
                        }
                        fn caller(flag: bool) { use(flag, 0); }
                    )",
                    .completes = false,
                },
                {
                    .name = "a failure from a handler guard leaves the try",
                    .source = R"(
                        struct Failure {}
                        fn use(flag: bool, const divisor: i32) throw Failure {
                            try { throw Failure {}; } catch {
                                Failure(_) if (if flag { throw Failure {}; true }
                                               else { throw Failure {}; true }) => {},
                                _ => {},
                            }
                            const _ = 1 / divisor;
                        }
                        fn caller(flag: bool) throw Failure { use(flag, 0)?; }
                    )",
                    .completes = false,
                },
                {
                    .name = "a failure from a selected handler body leaves the try",
                    .source = R"(
                        struct Failure {}
                        fn use(const divisor: i32) throw Failure {
                            try { throw Failure {}; } catch {
                                Failure(_) => { throw Failure {}; },
                                _ => {},
                            }
                            const _ = 1 / divisor;
                        }
                        fn caller() throw Failure { use(0)?; }
                    )",
                    .completes = false,
                },
                {
                    .name = "a test stop never enters a failure handler",
                    .source = R"(
                        fn use(const divisor: i32) {
                            try { fail(); } catch { _ => {}, }
                            const _ = 1 / divisor;
                        }
                        fn caller() { use(0); }
                    )",
                    .completes = false,
                },
            });
            ct::each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
                auto output = std::string();
                auto errors = std::string();
                const auto result =
                    compile_constant_program(std::string(scenario.source), output, errors);
                if (scenario.completes) {
                    if (ct::expect(!result.has_value())) {
                        ct::expect_diagnostic(result.error(), DiagnosticCode::ConstDivideByZero);
                    }
                } else {
                    auto findings = std::string();
                    if (!result) {
                        for (const auto& diagnostic : result.error()) {
                            findings += diagnostic.finding.message + "\n";
                        }
                    }
                    ct::expect(result.has_value()).note(findings);
                }
                ct::expect(output.empty());
                ct::expect(errors.empty());
            });
        }
    );
    ct::test("Compiler: pattern entries control static roots", [] static noexcept {
        struct Scenario final {
            std::string_view name;
            std::string_view source;
            bool enters_bound;
        };
        const auto scenarios = std::to_array<Scenario>({
            {
                .name = "a stopped payload excludes the next payload bound",
                .source = R"(
                    enum Value { Pair(i32, i32) }
                    fn use(value: Value, flag: bool, const divisor: i32) {
                        match value {
                            .Pair((if flag { fail(); 0 } else { fail(); 0 })..,
                                  (if flag { const _ = 1 / divisor; 0 }
                                   else { const _ = 1 / divisor; 0 })..) => {},
                            _ => {},
                        }
                    }
                    fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                )",
                .enters_bound = false,
            },
            {
                .name = "an accepted payload enters the next payload bound",
                .source = R"(
                    enum Value { Pair(i32, i32) }
                    fn use(value: Value, flag: bool, const divisor: i32) {
                        match value {
                            .Pair(0, (if flag { const _ = 1 / divisor; 0 }
                                      else { const _ = 1 / divisor; 0 })..) => {},
                            _ => {},
                        }
                    }
                    fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                )",
                .enters_bound = true,
            },
            {
                .name = "a stopped sole tag excludes the next alternative bound",
                .source = R"(
                    enum Value { Only(i32) }
                    fn use(value: Value, flag: bool, const divisor: i32) {
                        match value {
                            .Only((if flag { fail(); 0 } else { fail(); 0 })..)
                            | .Only((if flag { const _ = 1 / divisor; 0 }
                                     else { const _ = 1 / divisor; 0 })..) => {},
                            _ => {},
                        }
                    }
                    fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                )",
                .enters_bound = false,
            },
            {
                .name = "a rejected tag enters the next alternative bound",
                .source = R"(
                    enum Value { First(i32), Second(i32) }
                    fn use(value: Value, flag: bool, const divisor: i32) {
                        match value {
                            .First((if flag { fail(); 0 } else { fail(); 0 })..)
                            | .Second((if flag { const _ = 1 / divisor; 0 }
                                       else { const _ = 1 / divisor; 0 })..) => {},
                            _ => {},
                        }
                    }
                    fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                )",
                .enters_bound = true,
            },
        });
        ct::each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result =
                compile_constant_program(std::string(scenario.source), output, errors);
            if (scenario.enters_bound) {
                if (ct::expect(!result.has_value())) {
                    ct::expect_diagnostic(result.error(), DiagnosticCode::ConstDivideByZero);
                }
            } else {
                auto findings = std::string();
                if (!result) {
                    for (const auto& diagnostic : result.error()) {
                        findings += diagnostic.finding.message + "\n";
                    }
                }
                ct::expect(result.has_value()).note(findings);
            }
            ct::expect(output.empty());
            ct::expect(errors.empty());
        });
    });
    ct::test("Compiler: predicate continuation controls later static roots", [] static noexcept {
        struct Scenario final {
            std::string_view name;
            std::string_view source;
            bool enters_root;
        };
        const auto scenarios = std::to_array<Scenario>({
            {
                .name = "a terminal match guard excludes the next bound",
                .source = R"(
                    enum Value { Only(i32) }
                    fn use(value: Value, flag: bool, const divisor: i32) {
                        match value {
                            .Only(_) if (if flag { fail(); true } else { fail(); true }) => {},
                            .Only((if flag { const _ = 1 / divisor; 0 }
                                   else { const _ = 1 / divisor; 0 })..) => {},
                            _ => {},
                        }
                    }
                    fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                )",
                .enters_root = false,
            },
            {
                .name = "an ordinary match guard can reject into the next bound",
                .source = R"(
                    enum Value { Only(i32) }
                    fn use(value: Value, flag: bool, const divisor: i32) {
                        match value {
                            .Only(_) if flag => {},
                            .Only((if flag { const _ = 1 / divisor; 0 }
                                   else { const _ = 1 / divisor; 0 })..) => {},
                            _ => {},
                        }
                    }
                    fn caller(value: Value, flag: bool) { use(value, flag, 0); }
                )",
                .enters_root = true,
            },
            {
                .name = "a stopped catch predicate excludes the next handler bound",
                .source = R"(
                    enum Failure { Only(i32) }
                    fn use(flag: bool, const divisor: i32) {
                        try { throw Failure::Only(0); } catch {
                            Failure(.Only((if flag { fail(); 0 } else { fail(); 0 })..)) => {},
                            Failure(.Only((if flag { const _ = 1 / divisor; 0 }
                                           else { const _ = 1 / divisor; 0 })..)) => {},
                            _ => {},
                        }
                    }
                    fn caller(flag: bool) { use(flag, 0); }
                )",
                .enters_root = false,
            },
            {
                .name = "a completing catch predicate can reject into the next handler bound",
                .source = R"(
                    enum Failure { Only(i32) }
                    fn use(flag: bool, const divisor: i32) {
                        try { throw Failure::Only(0); } catch {
                            Failure(.Only((if flag { 0 } else { 0 })..)) => {},
                            Failure(.Only((if flag { const _ = 1 / divisor; 0 }
                                           else { const _ = 1 / divisor; 0 })..)) => {},
                            _ => {},
                        }
                    }
                    fn caller(flag: bool) { use(flag, 0); }
                )",
                .enters_root = true,
            },
            {
                .name = "a specialized stop excludes the next handler bound",
                .source = R"(
                    enum Failure { Only(i32) }
                    fn use(const stop: bool, const divisor: i32) {
                        try { throw Failure::Only(0); } catch {
                            Failure(.Only((const if stop { fail(); 0 } else { 0 })..)) => {},
                            Failure(.Only((if true { const _ = 1 / divisor; 0 }
                                           else { const _ = 1 / divisor; 0 })..)) => {},
                            _ => {},
                        }
                    }
                    fn caller() { use(true, 0); }
                )",
                .enters_root = false,
            },
            {
                .name = "a specialized completion retains the next handler bound",
                .source = R"(
                    enum Failure { Only(i32) }
                    fn use(const stop: bool, const divisor: i32) {
                        try { throw Failure::Only(0); } catch {
                            Failure(.Only((const if stop { fail(); 0 } else { 0 })..)) => {},
                            Failure(.Only((if true { const _ = 1 / divisor; 0 }
                                           else { const _ = 1 / divisor; 0 })..)) => {},
                            _ => {},
                        }
                    }
                    fn caller() { use(false, 0); }
                )",
                .enters_root = true,
            },
        });
        ct::each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result =
                compile_constant_program(std::string(scenario.source), output, errors);
            if (scenario.enters_root) {
                if (ct::expect(!result.has_value())) {
                    ct::expect_diagnostic(result.error(), DiagnosticCode::ConstDivideByZero);
                }
            } else {
                auto findings = std::string();
                if (!result) {
                    for (const auto& diagnostic : result.error()) {
                        findings += diagnostic.finding.message + "\n";
                    }
                }
                ct::expect(result.has_value()).note(findings);
            }
            ct::expect(output.empty());
            ct::expect(errors.empty());
        });
    });
    ct::test("Compiler: print executes only in static roots and const tests", [] static noexcept {
        auto output = std::string();
        auto errors = std::string();
        const auto result = compile_constant_program(
            R"(
        const fn observe(value: i32) {
            println("value", value);
            return value;
        }
        const initial = observe(2);
        fn runtime_only() { observe(9); }
        test "runtime body" { observe(8); }
        const test "static body" {
            check(initial == 2);
            for index in 0..2 { check(observe(index) == index); }
            if false { println("unexecuted"); }
            print("left"); println("right", true, '我');
            println();
            eprint("error"); eprintln(" stream");
            println(f"{12:04}");
            print("a\0b");
        }
    )",
            output,
            errors
        );
        auto diagnostic_report = std::string();
        if (!result) {
            for (const auto& diagnostic : result.error()) {
                diagnostic_report += diagnostic.finding.message + "\n";
            }
        }
        if (!(ct::expect(result.has_value()).note(diagnostic_report))) {
            return;
        }
        ct::expect(
            output
            == std::string("value 2\nvalue 0\nvalue 1\nleftright true 我\n\n0012\na")
                + std::string("\0b", 2)
        );
        ct::expect(errors == "error stream\n");
    });

    ct::test(
        "Compiler: static checks continue and requirements stop nested calls",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
        const fn message() { print("message;"); return "detail"; }
        const fn stop() { require(false, "stop"); print("unreachable"); }
        const test "checks" {
            check(true, message());
            check(false, message());
            print("continued;");
            stop();
            print("unreachable");
        }
        const test "next" { print("next;"); fail("last"); }
    )",
                output,
                errors
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect(output == "message;continued;next;");
            ct::expect(
                std::ranges::count_if(
                    result.error(),
                    [](const auto& diagnostic) static noexcept {
                        return diagnostic.finding.code == DiagnosticCode::ConstTest;
                    }
                )
                == 3
            );
            ct::expect(errors.empty());
        }
    );

    ct::test(
        "Compiler: static execution rejects an executed native construction",
        [] static noexcept {
            const auto cases = std::array {
                CompilerErrorExpectation {
                    .name = "native construction in const block",
                    .source =
                        "import <vector> using std::vector; const { let v = vector { 1, 2, 3 }; }",
                    .code = DiagnosticCode::ConstAdmission,
                    .primary_text = "vector { 1, 2, 3 }"
                },
            };
            check_compiler_errors(cases);
        }
    );

    ct::test(
        "Compiler: untaken native calls do not constrain static execution",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                "import(cpp) fn native(); const test \"t\" { if false { native(); } check(true); }",
                output,
                errors
            );
            ct::expect(result.has_value());
            ct::expect(output.empty());
            ct::expect(errors.empty());
        }
    );

    ct::test("Compiler: named callable values execute in constant tests", [] static noexcept {
        auto output = std::string();
        auto errors = std::string();
        const auto result = compile_constant_program(
            R"(const fn answer() -> i32 => 42;
           const test "indirect" {
               let selected = answer;
               check(selected() == 42);
               let view: fn() -> i32 = selected;
               check(view() == 42);
           })",
            output,
            errors
        );
        ct::expect(result.has_value());
        ct::expect(output.empty());
        ct::expect(errors.empty());
    });

    ct::test(
        "Compiler: constant calls through callable values require a const target",
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "ordinary function selected through a local callable",
                 .source = R"(fn answer() -> i32 => 42;
             const test "indirect" { let selected = answer; check(selected() == 42); })",
                 .code = DiagnosticCode::ConstAdmission,
                 .primary_text = "selected()"},
                {.name = "unprovable callback in a const function",
                 .source = R"(const fn invoke(callback: fn() -> i32) -> i32 => callback();)",
                 .code = DiagnosticCode::ConstAdmission,
                 .primary_text = "callback()"},
            });
            check_compiler_errors(cases);
        }
    );

    ct::test("Compiler: test reporting needs an active static test", [] static noexcept {
        auto output = std::string();
        auto errors = std::string();
        const auto result = compile_constant_program(
            R"(
        const fn checked() { check(true); return 1; }
        const value = checked();
    )",
            output,
            errors
        );
        if (!ct::expect(!result.has_value())) {
            return;
        }
        ct::expect_diagnostic(result.error(), DiagnosticCode::ConstTest);
    });

    ct::test(
        "Compiler: static print completes arguments before observing borrowed text",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
        const fn observe(value: i32) { print("argument;"); return value; }
        const test "arguments" {
            var number = 1;
            var text: String = "a";
            println(number, text, if true {
                number = 2;
                text.append("b");
                observe(number)
            } else { 0 });
        }
    )",
                output,
                errors
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            ct::expect(output == "argument;1 ab 2\n");
        }
    );

    ct::test(
        "Compiler: static failure diagnostics consume the root text budget",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto source =
                std::string("const test \"bounded\" { for index in 0..16 { check(false, \"")
                + std::string(1024uz * 1024uz - 128uz, 'x')
                + "\"); } } const test \"next\" { print(\"next\"); }";
            const auto result = compile_constant_program(source, output, errors);
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect_diagnostic(result.error(), DiagnosticCode::ConstLimit);
            ct::expect(output == "next");
            ct::expect(
                std::ranges::count_if(
                    result.error(),
                    [](const auto& diagnostic) static noexcept {
                        return diagnostic.finding.code == DiagnosticCode::ConstTest;
                    }
                )
                < 16
            );
        }
    );

    ct::test(
        "Compiler: const blocks execute regardless of runtime control and test selection",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
        const seed = 3;
        const { println("module", seed); }
        fn unused() {
            const offset = 2;
            if false {
                const {
                    var sum = 0;
                    for i in 0..seed { sum += i; }
                    println("local", sum + offset);
                    const inner = 9;
                    const { println("nested", inner); }
                }
            }
        }
        const test "retained" { check(seed == 3); println("test"); }
        const { later(); }
        const fn later() { eprintln("last"); }
    )",
                output,
                errors
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            auto lines = std::vector<std::string>();
            auto stream = std::istringstream(output);
            for (auto line = std::string(); std::getline(stream, line);) {
                lines.push_back(std::move(line));
            }
            std::ranges::sort(lines);
            ct::expect(
                lines == std::vector<std::string> {"local 5", "module 3", "nested 9", "test"}
            );
            ct::expect(errors == "last\n");
        }
    );

    ct::test(
        "Compiler: const blocks share control flow aggregates text and failure recovery",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
        struct Error { code: i32 }
        const fn read(ok: bool) -> i32 throw Error {
            if !ok { throw Error { code: 7 }; }
            return 12;
        }
        const {
            var values = [1, 2, 3];
            values[1] = read(true)?;
            var text = String {};
            for value in values { text.append_format(f"{value},"); }
            try { read(false)?; } catch { Error(error) => { println(error.code); } }
            println(text);
            println("done");
        }
    )",
                output,
                errors
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            ct::expect(output == "7\n1,12,3,\ndone\n");
            ct::expect(errors.empty());
        }
    );

    ct::test(
        "Compiler: const blocks diagnose stage boundaries and execution failures",
        [] static noexcept {
            struct Case final {
                std::string_view source;
                DiagnosticCode code;
            };

            const auto cases = std::array {
                Case {
                    "fn f(value: i32) { const { println(value); } }",
                    DiagnosticCode::ConstAdmission
                },
                Case {"const { check(true); }", DiagnosticCode::ConstTest},
                Case {"const { while true {} }", DiagnosticCode::ConstLimit},
                Case {"struct Error {} const { throw Error {}; }", DiagnosticCode::ConstEvaluation},
                Case {
                    R"(const { let value = c"x"; println(f"{value:p}"); })",
                    DiagnosticCode::ConstEvaluation
                },
                Case {
                    R"(const { let value = c"x"; println(value == value); })",
                    DiagnosticCode::ConstAdmission
                },
                Case {"const equal = c\"x\" == c\"x\";", DiagnosticCode::ConstInitializer},
            };
            ct::each(cases, &Case::source, [&](const auto& scenario) noexcept {
                auto output = std::string();
                auto errors = std::string();
                const auto result =
                    compile_constant_program(std::string(scenario.source), output, errors);
                if (!(ct::expect(!result.has_value()))) {
                    return;
                }
                ct::expect_diagnostic(result.error(), scenario.code);
            });
        }
    );

    ct::test(
        "Compiler: const blocks enforce lexical loans and stage isolation",
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "block loan prevents mutation even without a later read",
                 .source = R"(const {
             var text: String = "local";
             let view = text.as_str();
             text.clear();
         })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "text.clear()"},
                {.name = "local block loan prevents mutation even without a later read",
                 .source = R"(fn unused() { const {
             var text: String = "local";
             let view = text.as_str();
             text.clear();
         } })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "text.clear()"},
                {.name = "const block cannot capture an enclosing execution-frame value",
                 .source =
                     "fn f(value: i32) { const { let closure = [value]() => value; closure(); } }",
                 .code = DiagnosticCode::ConstAdmission,
                 .primary_text = "value"},
                {.name = "const block cannot write an enclosing runtime local",
                 .source = "fn f() { var value = 1; const { value = 2; } }",
                 .code = DiagnosticCode::ConstAdmission,
                 .primary_text = "value"},
                {.name = "const block cannot return from its function",
                 .source = "fn f() -> i32 { const { return 1; } return 2; }",
                 .code = DiagnosticCode::FlowTransferBoundary,
                 .primary_text = "return"},
                {.name = "const block cannot break an enclosing loop",
                 .source = "fn f() { for index in 0..3 { const { break; } } }",
                 .code = DiagnosticCode::FlowTransferBoundary,
                 .primary_text = "break"},
                {.name = "runtime local shadows a module constant across the stage boundary",
                 .source = "const value = 1; fn f(value: i32) { const { println(value); } }",
                 .code = DiagnosticCode::ConstAdmission,
                 .primary_text = "value"},
            });
            check_compiler_errors(cases);
        }
    );

    ct::test("Compiler: a static stage cannot call the function it belongs to", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "local constant",
             .source = "const fn value() -> i32 { const copy = value(); return 1; } "
                       "const initial = value();",
             .code = DiagnosticCode::ConstCycle,
             .primary_text = "value()"},
            {.name = "const block",
             .source = "const fn value() -> i32 { const { println(value()); } return 1; } "
                       "const initial = value();",
             .code = DiagnosticCode::ConstCycle,
             .primary_text = "value()"},
        });
        check_compiler_errors(cases);
    });

    ct::test(
        "Compiler: a block in a lambda body reads enclosing constants and its own",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
        fn unused() {
            const value = 7;
            const ignored = 8;
            let action = []() {
                const { const copy = value; const value = 9; println(copy, value); }
                const value = 11;
                println(value);
            };
            action();
        }
        )",
                output,
                errors
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            ct::expect(output == "7 9\n");
            ct::expect(errors.empty());
            if (!ct::expect(result->diagnostics.size() == 1uz)) {
                return;
            }
            ct::expect(result->diagnostics.front().finding.code == DiagnosticCode::LintUnusedLocal);
        }
    );

    ct::test(
        "Compiler: assertions fail static execution without requiring a test context",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
        const fn verify(value: i32) {
            assert(value == 2, "constant assertion");
            println("unreachable");
            return value;
        }
        const value = verify(1);
    )",
                output,
                errors
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect(output.empty());
            ct::expect(errors.empty());
            ct::expect(
                std::ranges::any_of(result.error(), [](const auto& diagnostic) static noexcept {
                    return diagnostic.finding.code == DiagnosticCode::AssertionFailed
                        && diagnostic.finding.message.contains("value: 1")
                        && diagnostic.finding.message.contains("constant assertion");
                })
            );
        }
    );

    ct::test(
        "Compiler: const blocks and static tests share labeled diagnostic context",
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view block;
                std::string_view context;
            };

            const auto scenarios = std::to_array<Scenario>({
                {"anonymous const block",
                 "const { divide(0); }",
                 "while evaluating this const block"},
                {"labeled const block",
                 "const \"table\" { divide(0); }",
                 "while evaluating this const block \"table\""},
                {"anonymous static test",
                 "const test { divide(0); }",
                 "while evaluating this const test"},
                {"named static test",
                 "const test \"division\" { divide(0); }",
                 "while evaluating this const test \"division\""},
                {"empty label",
                 "const \"\" { divide(0); }",
                 "while evaluating this const block \"\""},
                {"nested independent root",
                 "const \"outer\" { const \"inner\" { divide(0); } }",
                 "while evaluating this const block \"inner\""},
            });
            ct::each(scenarios, &Scenario::name, [&](const auto& scenario) noexcept {
                auto output = std::string();
                auto errors = std::string();
                const auto result = compile_constant_program(
                    std::string("const fn divide(divisor: i32) -> i32 => 1 / divisor; ")
                        + std::string(scenario.block),
                    output,
                    errors
                );
                if (!(ct::expect(!result.has_value()))) {
                    return;
                }
                const auto* diagnostic =
                    ct::find_diagnostic(result.error(), DiagnosticCode::ConstDivideByZero);
                if (!(ct::expect(diagnostic != nullptr))) {
                    return;
                }
                if (!(ct::expect_equal(diagnostic->attachment.related.size(), 2uz))) {
                    return;
                }
                ct::expect_equal(diagnostic->attachment.related.back().message, scenario.context);
                ct::expect_equal(
                    diagnostic->attachment.related.front().message,
                    std::string_view("while evaluating this const function call")
                );
                ct::expect(output.empty());
                ct::expect(errors.empty());
            });
        }
    );
    ct::test(
        "Compiler: staged argument roots execute per occurrence and body roots per instance",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
        const fn argument() -> i32 { print("A"); return 2; }
        const fn body_root(value: i32) -> i32 { print("R"); return value + 1; }
        fn add(value: i32, const amount: i32) -> i32 {
            const adjusted = body_root(amount);
            return value + adjusted;
        }
        fn first(value: i32) -> i32 => add(value, argument());
        fn second(value: i32) -> i32 => add(value, argument());
    )",
                output,
                errors
            );
            if (!result) {
                for (const auto& diagnostic : result.error()) {
                    ct::expect(result.has_value()).note(diagnostic.finding.message);
                }
            }
            if (!ct::expect(result.has_value())) {
                return;
            }
            ct::expect(std::ranges::count(output, 'A') == 2);
            ct::expect(std::ranges::count(output, 'R') == 1);
            ct::expect(output.size() == 3uz);
            ct::expect(errors.empty());
        }
    );

    ct::test("Compiler: argument effects follow their execution stage", [] static noexcept {
        struct Input final {
            std::string_view name;
            std::string_view source;
            std::string_view output;
        };
        const auto cases = std::to_array<Input>({
            {.name = "required declaration call",
             .source =
                 "const result = target(runtime_input(1), static_input(1), runtime_input(2), static_input(2));",
             .output = "r1s1r2s2s3b"},
            {.name = "static execution through an ordinary const function body",
             .source =
                 "const fn use() -> i32 => target(runtime_input(1), static_input(1), runtime_input(2), static_input(2)); const result = use();",
             .output = "s1s2s3r1r2b"},
            {.name = "required test body",
             .source =
                 "const test { check(target(runtime_input(1), static_input(1), runtime_input(2), static_input(2)) == 9); }",
             .output = "r1s1r2s2s3b"},
            {.name = "native body retains residual effects for execution",
             .source =
                 "fn use() -> i32 => target(runtime_input(1), static_input(1), runtime_input(2), static_input(2));",
             .output = "s1s2s3"},
        });
        ct::each(cases, &Input::name, [](const auto& input) static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                std::string(R"(
                    const fn static_input(value: i32) -> i32 { print(f"s{value}"); return value; }
                    const fn runtime_input(value: i32) -> i32 { print(f"r{value}"); return value; }
                    const fn target(left: i32, const first: i32, right: i32, const second: i32) -> i32 {
                        const anchor = static_input(3);
                        print("b");
                        return left + first + right + second + anchor;
                    }
                )") + std::string(input.source),
                output,
                errors
            );
            ct::expect(result.has_value());
            ct::expect_equal(output, std::string(input.output));
            ct::expect(errors.empty());
        });
    });

    ct::test(
        "Compiler: local constants freeze owning text at every static occurrence",
        [] static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                R"(
            const fn label(value: i32) -> String => f"{value}";
            const fn use(const value: i32) -> str {
                const text = label(value);
                return text;
            }
            fn independent() -> str { const text = label(2); return text; }
            const test { check(use(2) == "2"); check(use(3) == "3"); }
        )",
                output,
                errors
            );
            ct::expect(result.has_value());
            ct::expect(output.empty());
            ct::expect(errors.empty());
        }
    );

    ct::test("Compiler: static roots execute once per live instance identity", [] static noexcept {
        struct Input final {
            std::string_view name;
            std::string_view source;
            std::string_view output;
        };
        const auto cases = std::to_array<Input>({
            {.name = "empty const for",
             .source = R"(
            fn use(const amount: i32) { const for index in 0..0 { const value = provider(amount); lane(index); } }
            fn caller() { use(7); }
        )",
             .output = ""},
            {.name = "provider executes once for each expanded identity",
             .source = R"(
            fn use(const amount: i32) { const for index in 0..3 { const value = provider(amount); lane(index); } }
            fn caller() { use(7); }
        )",
             .output = "root\nroot\nroot\n"},
            {.name = "runtime loop keeps one constant identity",
             .source = R"(
            fn use(count: i32, const amount: i32) {
                for index in 0..count { const value = provider(amount); }
            }
            fn caller(count: i32) { use(count, 7); }
        )",
             .output = "root\n"},
            {.name = "static execution and native code share one instance",
             .source = R"(
            const fn repeat(const amount: i32) -> i32 {
                for index in 0..2 { const value = provider(amount); }
                return amount;
            }
            const result = repeat(3);
            fn caller() -> i32 => repeat(3);
        )",
             .output = "root\n"},
            {.name = "runtime loop keeps one identity",
             .source = R"(
            const fn use(count: i32, const enabled: bool, const amount: i32) {
                for index in 0..count {
                    const value = provider(amount);
                    const if !enabled { continue; }
                    lane(1);
                }
            }
            const test { use(3, false, 7); }
        )",
             .output = "root\n"},
            {.name = "expansion uses each index",
             .source = R"(
            const fn use(const count: i32, const amount: i32) {
                const for index in 0..count { const value = provider(amount); lane(index); }
            }
            const test { use(3, 7); }
        )",
             .output = "root\nroot\nroot\n"},
            {.name = "empty range executes no provider",
             .source = R"(
            const fn use(const count: i32, const amount: i32) {
                const for index in 0..count { const value = provider(amount); lane(index); }
            }
            const test { use(0, 7); }
        )",
             .output = ""},
            {.name = "empty expansion omits independent local roots",
             .source = R"(
            fn use() { const for index in 0..0 { const value = provider(7); lane(index); } }
        )",
             .output = ""},
            {.name = "unselected static arm omits independent local roots",
             .source =
                 R"(fn use(const enabled: bool) { const if enabled { const value = provider(7); } } fn caller() { use(false); })",
             .output = ""},
            {.name = "selected static arm executes independent local roots",
             .source =
                 R"(fn use(const enabled: bool) { const if enabled { const value = provider(7); } } fn caller() { use(true); })",
             .output = "root\n"},
            {.name = "ordinary inactive arm keeps independent roots required",
             .source = R"(fn use() { if false { const value = provider(7); } })",
             .output = "root\n"},
            {.name = "independent root has one occurrence in each expanded region",
             .source = R"(fn use() { const for index in 0..3 { const value = provider(7); } })",
             .output = "root\nroot\nroot\n"},
            {.name = "a type reads a root without executing its declaration",
             .source =
                 R"(fn use() { const for index in 0..3 { const count = provider(2); let values: [i32; count] = [1, 2]; } })",
             .output = "root\nroot\nroot\n"},
            {.name = "a type in an unselected arm executes no root",
             .source =
                 R"(fn use() { const if false { const count = provider(2); let values: [i32; count] = [1, 2]; } })",
             .output = ""},
            {.name = "a const block executes once for each expansion and reads its index",
             .source =
                 R"(fn use(const amount: i32) { const for index in 0..3 { const { println(amount + index); } } } fn caller() { use(7); })",
             .output = "7\n8\n9\n"},
            {.name = "a const block in an unselected arm does not execute",
             .source =
                 R"(fn use(const enabled: bool) { const if enabled { const { println("selected"); } } } fn caller() { use(false); })",
             .output = ""},
            {.name = "a const block executes after the roots before it",
             .source =
                 R"(const fn use() -> i32 { const value = provider(2); const { println(value); } return value; } const answer = use();)",
             .output = "root\n2\n"},
            {.name = "a const block reads the innermost binding of a shadowed root",
             .source =
                 R"(fn use() { const value = provider(2); if true { const value = value + 1; const { println(value); } } })",
             .output = "root\n3\n"},
            {.name = "a lambda body reads a root that its enclosing body executes",
             .source =
                 R"(fn use() { const value = provider(2); let closure = []() -> i32 => value; closure(); })",
             .output = "root\n"},
            {.name = "a runtime call is not promoted by its static argument",
             .source = R"(
            const fn runtime(const amount: i32) -> i32 { println("runtime"); return amount; }
            fn caller() -> i32 => runtime(provider(2));
        )",
             .output = "root\n"},
            {.name = "ordinary short circuit keeps static arguments required",
             .source =
                 R"(fn use(const amount: i32) -> bool => false && (lane(provider(amount)) == 7); fn caller() -> bool => use(7);)",
             .output = "root\n"},
            {.name = "static input in an ordinary short circuit keeps arguments required",
             .source =
                 R"(fn use(const enabled: bool, const amount: i32) -> bool => enabled && (lane(provider(amount)) == 7); fn caller() -> bool => use(false, 7);)",
             .output = "root\n"},
            {.name = "ordinary branches keep every static root required",
             .source =
                 R"(fn use(flag: bool, const enabled: bool, const amount: i32) { if flag {} else if enabled { const value = provider(amount); lane(provider(amount)); } } fn caller(flag: bool) { use(flag, false, 7); })",
             .output = "root\nroot\n"},
            {.name = "a static exit skips later roots while an ordinary exit does not",
             .source = R"(
            fn ordinary(dynamic: bool, const enabled: bool, const amount: i32) -> i32 {
                if enabled { return 7; }
                if dynamic { return lane(provider(amount)); }
                return 0;
            }
            fn selected(dynamic: bool, const enabled: bool, const amount: i32) -> i32 {
                const if enabled { return 7; }
                if dynamic { return lane(provider(amount)); }
                return 0;
            }
            fn caller(dynamic: bool) -> i32 => ordinary(dynamic, true, 2) + selected(dynamic, true, 3);
        )",
             .output = "root\n"},
            {.name = "a conditional report keeps later roots when its message cannot complete",
             .source = R"(fn use(flag: bool, const amount: i32) -> i32 {
                 assert(true, if flag { fail(); "first" } else { fail(); "second" });
                 const value = provider(amount); return value;
             } fn caller(flag: bool) -> i32 => use(flag, 7);)",
             .output = "root\n"},
            {.name = "runtime division stays a runtime operation",
             .source =
                 R"(fn use(flag: bool, const divisor: i32) -> bool { if flag { return true; } else if 1 / divisor == 1 { return false; } return true; } fn caller(flag: bool) -> bool => use(flag, 0);)",
             .output = ""},
            {.name = "unselected const if arm evaluates no static root",
             .source =
                 R"(fn use(const enabled: bool, const amount: i32) { const if enabled { const value = provider(amount); lane(provider(amount)); } } fn caller() { use(false, 7); })",
             .output = ""},
            {.name = "selected const if arm expands its const for roots",
             .source = R"(
            fn use(const enabled: bool, const count: i32) {
                const if enabled { const for index in 0..count { const value = provider(index); } }
            }
            fn caller() { use(true, 2); use(false, 3); }
        )",
             .output = "root\nroot\n"},
            {.name = "const if condition executes once per instance",
             .source =
                 R"(fn use(const amount: i32) { const if provider(amount) == 7 {} } fn caller() { use(7); use(7); })",
             .output = "root\n"},
            {.name = "const if skips an unselected arm",
             .source =
                 R"(const fn use(count: i32, const enabled: bool, const amount: i32) { for index in 0..count { const if enabled { lane(provider(amount)); } } } const test { use(3, false, 7); })",
             .output = ""},
            {.name = "nested conditional roots stay required",
             .source =
                 R"(fn use(const amount: i32) { if false && (if true { const value = provider(amount); true } else { false }) {} } fn caller() { use(7); })",
             .output = "root\n"},
            {.name = "expanded condition roots use each index",
             .source =
                 R"(const fn use(const count: i32, const amount: i32) { const for index in 0..count { if false && (if true { const value = provider(amount + index); true } else { false }) {} } } const test { use(3, 7); })",
             .output = "root\nroot\nroot\n"},
        });
        ct::each(cases, &Input::name, [](const auto& input) static noexcept {
            auto output = std::string();
            auto errors = std::string();
            const auto result = compile_constant_program(
                std::string(R"(
            const fn provider(amount: i32) -> i32 { println("root"); return amount; }
            const fn lane(const index: i32) -> i32 => index;
        )") + std::string(input.source),
                output,
                errors
            );
            ct::expect(result.has_value());
            ct::expect_equal(output, std::string(input.output));
            ct::expect(errors.empty());
        });
    });
});

} // namespace
