module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.interpreter.execute;

import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
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
        InterpreterOptions {.limits = constant_execution_limits(), .trace = {}}
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
        InterpreterOptions {.limits = constant_execution_limits(), .trace = {}}
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
        InterpreterOptions {.limits = constant_execution_limits(), .trace = {}}
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
    const auto result = interpret(
        program,
        entry(program),
        {},
        InterpreterOptions {.limits = constant_execution_limits(), .trace = {}}
    );
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
        InterpreterOptions {.limits = constant_execution_limits(), .trace = {}}
    );
    REQUIRE(!result.has_value());
    CHECK(result.error().code == DiagnosticCode::InterpretAdmission);
    CHECK(output.empty());
}

TEST_CASE("Interpreter: static and dynamic ranges select the matching branch") {
    const auto program = analyze_test_program(R"(
        fn classify(score: i32) -> str {
            return match score {
                ..0 => "invalid",
                0..60 => "retry",
                60..=100 => "pass",
                101.. => "invalid",
            };
        }
        fn within(value: i32, low: i32, high: i32) -> str {
            return match value {
                low..=high => "inside",
                _ => "outside",
            };
        }
        println(classify(-1), classify(59), classify(60), classify(100), classify(101));
        println(within(3, 3, 3), within(3, 4, 2));
    )");
    const auto types = program.types().size();
    const auto constants = program.constants().size();
    const auto values = PublishedConstantValues(program);
    for (const auto kind : builtin_types) {
        CHECK(values.builtin_type(kind) == program.types().builtin_type(kind));
    }
    for (auto attempt = 0; attempt < 2; ++attempt) {
        auto output = std::string();
        const auto result = interpret(
            program,
            entry(program),
            [&](ExecutionOutputStream, std::string_view bytes) noexcept { output.append(bytes); },
            InterpreterOptions {.limits = constant_execution_limits(), .trace = {}}
        );
        REQUIRE(result.has_value());
        CHECK(output == "invalid retry pass pass invalid\ninside outside\n");
        CHECK(program.types().size() == types);
        CHECK(program.constants().size() == constants);
    }
}
