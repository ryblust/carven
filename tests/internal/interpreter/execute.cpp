module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.interpreter.execute;

import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.semir.decl;
import :semantic.semir.program;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto entry(const SemIRProgram& program) noexcept -> FunctionID {
    auto selected = std::optional<FunctionID>();
    for (const auto row : program.declarations().functions()) {
        if (row.value.entry_point) {
            selected = row.id;
        }
    }
    REQUIRE(selected.has_value());
    return *selected;
}

} // namespace

TEST_CASE("Interpreter: argument observation follows shared storage and evaluation rules") {
    const auto program = analyze_test_program(R"(
        fn observe(value: i32) { print("argument;"); return value; }
        var number = 1;
        var text: String = "a";
        println(number, text, if true {
            number = 2;
            text.append("b");
            observe(number)
        } else { 0 });
    )");
    auto output = std::string();
    const auto constants = program.constants().size();
    const auto types = program.types().size();
    const auto spellings = program.provenance().spellings().size();
    const auto result = interpret(
        program,
        entry(program),
        [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            CHECK(stream == ExecutionOutputStream::Standard);
            output.append(bytes);
        },
        InterpreterOptions {.limits = {}, .trace = {}}
    );
    REQUIRE(result.has_value());
    CHECK(output == "argument;1 ab 2\n");
    CHECK(program.constants().size() == constants);
    CHECK(program.types().size() == types);
    CHECK(program.provenance().spellings().size() == spellings);
}

TEST_CASE("Interpreter: unused native functions do not constrain executed bodies") {
    const auto program = analyze_test_program(R"(
        private import(cpp) fn native();
        fn unused() { native(); }
        println("ready");
    )");
    auto output = std::string();
    const auto result = interpret(
        program,
        entry(program),
        [&](ExecutionOutputStream, std::string_view bytes) noexcept { output.append(bytes); },
        InterpreterOptions {.limits = {}, .trace = {}}
    );
    REQUIRE(result.has_value());
    CHECK(output == "ready\n");
}

TEST_CASE("Interpreter: transitive admission rejects untaken unsupported calls before output") {
    const auto program = analyze_test_program(R"(
        private import(cpp) fn native();
        fn helper() { if false { native(); } }
        println("not executed");
        helper();
    )");
    auto output = std::string();
    const auto result = interpret(
        program,
        entry(program),
        [&](ExecutionOutputStream, std::string_view bytes) noexcept { output.append(bytes); },
        InterpreterOptions {.limits = {}, .trace = {}}
    );
    REQUIRE(!result.has_value());
    CHECK(result.error().code == DiagnosticCode::InterpretAdmission);
    CHECK(output.empty());
}

TEST_CASE("Interpreter: budgets cover ordinary recursive calls") {
    const auto program = analyze_test_program(R"(
        fn recurse(value: i32) -> i32 { return recurse(value); }
        recurse(1);
    )");
    const auto result =
        interpret(program, entry(program), {}, InterpreterOptions {.limits = {}, .trace = {}});
    REQUIRE(!result.has_value());
    CHECK(result.error().code == DiagnosticCode::InterpretLimit);
    CHECK(!result.error().calls.empty());
}

TEST_CASE("Interpreter: native source initialization cannot be silently omitted") {
    const auto program = analyze_test_program(R"(
        #[cpp] ---
        inline int native_state = [] { return 42; }();
        ---
        println("not executed");
    )");
    auto output = std::string();
    const auto result = interpret(
        program,
        entry(program),
        [&](ExecutionOutputStream, std::string_view bytes) noexcept { output.append(bytes); },
        InterpreterOptions {.limits = {}, .trace = {}}
    );
    REQUIRE(!result.has_value());
    CHECK(result.error().code == DiagnosticCode::InterpretAdmission);
    CHECK(output.empty());
}
