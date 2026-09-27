module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.constant_execution;

import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :semantic.evaluation.output;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

auto compile_constant_program(
    std::string source,
    std::string& output,
    std::string& errors
) noexcept {
    auto sources = SourceManager();
    const auto id = *sources.append_virtual("constant_execution.cv", std::move(source));
    const auto input = SourceModuleInput {
        .source_id = id,
        .module_path = *CanonicalModulePath::from_value("constant_execution")
    };
    return compile(
        sources,
        SourceBatch {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("constant-execution")
        },
        [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            (stream == ExecutionOutputStream::Standard ? output : errors) += bytes;
        }
    );
}

} // namespace

TEST_CASE("Compiler: print executes only in required constant evaluations and const tests") {
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
    if (!result) {
        for (const auto& diagnostic : result.error()) {
            INFO(diagnostic.finding.message);
        }
    }
    REQUIRE(result.has_value());
    CHECK(
        output
        == std::string("value 2\nvalue 0\nvalue 1\nleftright true 我\n\n0012\na")
            + std::string("\0b", 2)
    );
    CHECK(errors == "error stream\n");
}

TEST_CASE("Compiler: static checks continue and requirements stop nested calls") {
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
    REQUIRE(!result.has_value());
    CHECK(output == "message;continued;next;");
    CHECK(
        std::ranges::count_if(
            result.error(),
            [](const auto& diagnostic) static noexcept {
                return diagnostic.finding.code == DiagnosticCode::ConstTest;
            }
        )
        == 3
    );
    CHECK(errors.empty());
}

TEST_CASE("Compiler: required execution rejects an executed native construction") {
    const auto cases = std::array {
        CompilerErrorExpectation {
            .name = "native construction in constant block",
            .source = "import <vector> using std::vector; const { let v = vector { 1, 2, 3 }; }",
            .code = "CV-CONST-ADMISSION",
            .primary_text = "vector { 1, 2, 3 }"
        },
    };
    check_compiler_errors(cases);
}

TEST_CASE("Compiler: untaken native calls do not constrain constant execution") {
    auto output = std::string();
    auto errors = std::string();
    const auto result = compile_constant_program(
        "import(cpp) fn native(); const test \"t\" { if false { native(); } check(true); }",
        output,
        errors
    );
    CHECK(result.has_value());
    CHECK(output.empty());
    CHECK(errors.empty());
}

TEST_CASE("Compiler: named callable values execute in constant tests") {
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
    CHECK(result.has_value());
    CHECK(output.empty());
    CHECK(errors.empty());
}

TEST_CASE("Compiler: constant calls through callable values require a const target") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "ordinary function selected through a local callable",
         .source = R"(fn answer() -> i32 => 42;
             const test "indirect" { let selected = answer; check(selected() == 42); })",
         .code = "CV-CONST-ADMISSION",
         .primary_text = "selected()"},
        {.name = "unprovable callback in a const function",
         .source = R"(const fn invoke(callback: fn() -> i32) -> i32 => callback();)",
         .code = "CV-CONST-ADMISSION",
         .primary_text = "callback()"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler: test reporting needs an active static test") {
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
    REQUIRE(!result.has_value());
    CHECK(find_compiler_diagnostic(result.error(), "CV-CONST-TEST") != nullptr);
}

TEST_CASE("Compiler: static print completes arguments before observing borrowed text") {
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
    REQUIRE(result.has_value());
    CHECK(output == "argument;1 ab 2\n");
}

TEST_CASE("Compiler: static failure diagnostics consume the root text budget") {
    auto output = std::string();
    auto errors = std::string();
    const auto source = std::string("const test \"bounded\" { for index in 0..16 { check(false, \"")
        + std::string(1024uz * 1024uz - 128uz, 'x')
        + "\"); } } const test \"next\" { print(\"next\"); }";
    const auto result = compile_constant_program(source, output, errors);
    REQUIRE(!result.has_value());
    CHECK(find_compiler_diagnostic(result.error(), "CV-CONST-LIMIT") != nullptr);
    CHECK(output == "next");
    CHECK(
        std::ranges::count_if(
            result.error(),
            [](const auto& diagnostic) static noexcept {
                return diagnostic.finding.code == DiagnosticCode::ConstTest;
            }
        )
        < 16
    );
}

TEST_CASE("Compiler: constant blocks execute independently of runtime control and test selection") {
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
                    return;
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
    REQUIRE(result.has_value());
    auto lines = std::vector<std::string>();
    auto stream = std::istringstream(output);
    for (auto line = std::string(); std::getline(stream, line);) {
        lines.push_back(std::move(line));
    }
    std::ranges::sort(lines);
    CHECK(lines == std::vector<std::string> {"local 5", "module 3", "nested 9", "test"});
    CHECK(errors == "last\n");
}

TEST_CASE("Compiler: constant blocks share control flow aggregates text and failure recovery") {
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
            return println("done");
        }
    )",
        output,
        errors
    );
    REQUIRE(result.has_value());
    CHECK(output == "7\n1,12,3,\ndone\n");
    CHECK(errors.empty());
}

TEST_CASE("Compiler: constant blocks diagnose stage boundaries and execution failures") {
    struct Case final {
        std::string_view source;
        std::string_view code;
    };

    const auto cases = std::array {
        Case {"fn f(value: i32) { const { println(value); } }", "CV-CONST-ADMISSION"},
        Case {"const { check(true); }", "CV-CONST-TEST"},
        Case {"const { while true {} }", "CV-CONST-LIMIT"},
        Case {"struct Error {} const { throw Error {}; }", "CV-CONST-EVALUATION"},
        Case {R"(const { let value = c"x"; println(f"{value:p}"); })", "CV-CONST-EVALUATION"},
        Case {R"(const { let value = c"x"; println(value == value); })", "CV-CONST-ADMISSION"},
        Case {"const equal = c\"x\" == c\"x\";", "CV-CONST-INITIALIZER"},
    };
    for (const auto& scenario : cases) {
        CAPTURE(scenario.source);
        auto output = std::string();
        auto errors = std::string();
        const auto result = compile_constant_program(std::string(scenario.source), output, errors);
        REQUIRE(!result.has_value());
        CHECK(find_compiler_diagnostic(result.error(), scenario.code) != nullptr);
    }
}

TEST_CASE("Compiler: constant blocks enforce lexical loans and stage isolation") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "block loan prevents mutation even without a later read",
         .source = R"(const {
             var text: String = "local";
             let view = text.as_str();
             text.clear();
         })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.clear()"},
        {.name = "local block loan prevents mutation even without a later read",
         .source = R"(fn unused() { const {
             var text: String = "local";
             let view = text.as_str();
             text.clear();
         } })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.clear()"},
        {.name = "constant block cannot capture an enclosing execution-frame value",
         .source = "fn f(value: i32) { const { let closure = [value]() => value; closure(); } }",
         .code = "CV-CONST-ADMISSION",
         .primary_text = "value"},
        {.name = "runtime local shadows a module constant across the stage boundary",
         .source = "const value = 1; fn f(value: i32) { const { println(value); } }",
         .code = "CV-CONST-ADMISSION",
         .primary_text = "value"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler: constant blocks execute once after their callable dependencies are complete") {
    auto output = std::string();
    auto errors = std::string();
    const auto result = compile_constant_program(
        R"(
        const initial = value();
        const fn value() {
            const { const copy = value(); println(copy); }
            return 1;
        }
        const test "value" { check(initial == 1); check(value() == 1); }
        )",
        output,
        errors
    );
    REQUIRE(result.has_value());
    CHECK(output == "1\n");
    CHECK(errors.empty());
}

TEST_CASE("Compiler: deferred blocks preserve lexical snapshots and constant usage") {
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
    REQUIRE(result.has_value());
    CHECK(output == "7 9\n");
    CHECK(errors.empty());
    REQUIRE(result->diagnostics.size() == 1uz);
    CHECK(result->diagnostics.front().finding.code == DiagnosticCode::LintUnusedLocal);
}

TEST_CASE("Compiler: assertions fail constant execution without requiring a test context") {
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
    REQUIRE(!result.has_value());
    CHECK(output.empty());
    CHECK(errors.empty());
    CHECK(std::ranges::any_of(result.error(), [](const auto& diagnostic) static noexcept {
        return diagnostic.finding.code == DiagnosticCode::AssertionFailed
            && diagnostic.finding.message.contains("value: 1")
            && diagnostic.finding.message.contains("constant assertion");
    }));
}

TEST_CASE("Compiler: compile-time blocks share labeled diagnostic context") {
    struct Scenario final {
        std::string_view name;
        std::string_view block;
        std::string_view context;
    };

    const auto scenarios = std::to_array<Scenario>({
        {"anonymous constant block",
         "const { divide(0); }",
         "while evaluating this constant block"},
        {"labeled constant block",
         "const \"table\" { divide(0); }",
         "while evaluating this constant block \"table\""},
        {"anonymous static test", "const test { divide(0); }", "while evaluating this const test"},
        {"named static test",
         "const test \"division\" { divide(0); }",
         "while evaluating this const test \"division\""},
        {"empty label", "const \"\" { divide(0); }", "while evaluating this constant block \"\""},
        {"nested independent root",
         "const \"outer\" { const \"inner\" { divide(0); } }",
         "while evaluating this constant block \"inner\""},
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        auto output = std::string();
        auto errors = std::string();
        const auto result = compile_constant_program(
            std::string("const fn divide(divisor: i32) -> i32 => 1 / divisor; ")
                + std::string(scenario.block),
            output,
            errors
        );
        REQUIRE(!result.has_value());
        const auto* diagnostic =
            find_compiler_diagnostic(result.error(), "CV-CONST-DIVIDE-BY-ZERO");
        REQUIRE(diagnostic != nullptr);
        REQUIRE_EQ(diagnostic->attachment.related.size(), 2uz);
        CHECK_EQ(diagnostic->attachment.related.back().message, scenario.context);
        CHECK_EQ(
            diagnostic->attachment.related.front().message,
            "while evaluating this const function call"
        );
        CHECK(output.empty());
        CHECK(errors.empty());
    }
}
