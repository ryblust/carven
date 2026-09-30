module carven:test.internal.interpreter.execute;

import :diagnostics.code;
import :interpreter.execute;
import :semantic.evaluation.display;
import :semantic.evaluation.execution;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

auto entry(const SemIRProgram& program) noexcept -> FunctionID {
    auto selected = std::optional<FunctionID>();
    for (const auto row : program.declarations().functions()) {
        if (row.value.entry_point) {
            selected = row.id;
        }
    }
    ct::require(selected.has_value());
    return *selected;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Interpreter: argument observation follows shared storage and evaluation rules",
        [] static noexcept {
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
                    ct::expect(stream == ExecutionOutputStream::Standard);
                    output.append(bytes);
                },
                InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            ct::expect(output == "argument;1 ab 2\n");
            ct::expect(program.constants().size() == constants);
            ct::expect(program.types().size() == types);
            ct::expect(program.provenance().spellings().size() == spellings);
        }
    );

    ct::test(
        "Interpreter: unused native functions do not constrain executed bodies",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        private import(cpp) fn native();
        fn unused() { native(); }
        println("ready");
    )");
            auto output = std::string();
            const auto result = interpret(
                program,
                entry(program),
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    output.append(bytes);
                },
                InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            ct::expect(output == "ready\n");
        }
    );

    ct::test(
        "Interpreter: native admission follows executed paths and preserves preceding effects",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        private import(cpp) fn native();
        fn helper(selected: bool) { if selected { native(); } }
        println("before");
        helper(false);
        println("after skipped call");
        helper(true);
        println("after native call");
    )");
            auto output = std::string();
            const auto result = interpret(
                program,
                entry(program),
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    output.append(bytes);
                },
                InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect(
                interpreter_diagnostic_code(result.error().event.reason())
                == DiagnosticCode::InterpretAdmission
            );
            ct::expect(output == "before\nafter skipped call\n");
        }
    );

    ct::test(
        "Interpreter: native capability is checked after operands complete",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        import <cstdlib>;
        struct Failure {}
        fn operand(failing: bool) -> i32 throw Failure {
            println("operand");
            if failing { throw Failure {}; }
            return 1;
        }
        fn attempt(failing: bool) -> i32 {
            return try {
                ::std::abs(operand(failing)?) as i32
            } catch { Failure(_) => 7, };
        }
        println(attempt(true));
        println(attempt(false));
    )");
            auto output = std::string();
            const auto result = interpret(
                program,
                entry(program),
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    output.append(bytes);
                },
                InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect(
                interpreter_diagnostic_code(result.error().event.reason())
                == DiagnosticCode::InterpretAdmission
            );
            ct::expect(output == "operand\n7\noperand\n");
        }
    );

    ct::test("Interpreter: budgets cover ordinary recursive calls", [] static noexcept {
        const auto program = analyze_test_program(R"(
        fn recurse(value: i32) -> i32 { return recurse(value); }
        recurse(1);
    )");
        const auto result = interpret(
            program,
            entry(program),
            {},
            InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
        );
        if (!ct::expect(!result.has_value())) {
            return;
        }
        ct::expect(
            interpreter_diagnostic_code(result.error().event.reason())
            == DiagnosticCode::InterpretLimit
        );
        ct::expect(!result.error().event.calls.empty());
    });

    ct::test(
        "Interpreter: native source initialization cannot be silently omitted",
        [] static noexcept {
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
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    output.append(bytes);
                },
                InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect(
                interpreter_diagnostic_code(result.error().event.reason())
                == DiagnosticCode::InterpretAdmission
            );
            ct::expect(output.empty());
        }
    );

    ct::test(
        "Interpreter: static and dynamic ranges select the matching branch",
        [] static noexcept {
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
                ct::expect(values.builtin_type(kind) == program.types().builtin_type(kind));
            }
            for (auto attempt = 0; attempt < 2; ++attempt) {
                auto output = std::string();
                const auto result = interpret(
                    program,
                    entry(program),
                    [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                        output.append(bytes);
                    },
                    InterpreterOptions {
                        .limits = static_execution_limits(),
                        .trace = {},
                        .report = {}
                    }
                );
                if (!ct::expect(result.has_value())) {
                    return;
                }
                ct::expect(output == "invalid retry pass pass invalid\ninside outside\n");
                ct::expect(program.types().size() == types);
                ct::expect(program.constants().size() == constants);
            }
        }
    );

    ct::test(
        "Interpreter: runtime tests execute published bodies without admitting the program entry",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        private import(cpp) fn native();
        fn main() { native(); }
        const test "static" { check(true); }
        fn helper() { check(false, "first"); require(false, "stop"); }
        struct Problem {}
        fn recoverable() throw Problem { throw Problem {}; }
        test "failed" {
            try { helper(); recoverable()?; }
            catch { Problem(_) => { println("unexpected recovery"); } }
            println("unreachable");
        }
        fn stop() { fail("explicit failure"); }
        test "explicit" { stop(); println("unreachable"); }
        test "next" { println("next"); }
    )");
            const auto constants = program.constants().size();
            const auto types = program.types().size();
            for (auto attempt = 0; attempt < 2; ++attempt) {
                auto output = std::string();
                const auto results = interpret_tests(
                    program,
                    [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                        output.append(bytes);
                    },
                    InterpreterOptions {
                        .limits = static_execution_limits(),
                        .trace = {},
                        .report = {}
                    }
                );
                if (!ct::expect(results.has_value())) {
                    return;
                }
                if (!ct::expect(results->size() == 3)) {
                    return;
                }
                ct::expect(
                    block_display_name(
                        program.provenance(),
                        program.tests().test((*results)[0].test).source
                    )
                    == "failed"
                );
                if (!ct::expect((*results)[0].reports.size() == 2)) {
                    return;
                }
                ct::expect(
                    interpreter_diagnostic_code((*results)[0].reports[0].reason())
                    == DiagnosticCode::InterpretExecution
                );
                ct::expect(execution_message((*results)[0].reports[0]).contains("first"));
                ct::expect(execution_message((*results)[0].reports[1]).contains("stop"));
                ct::expect(!(*results)[0].reports[1].calls.empty());
                ct::expect((*results)[0].termination == ExecutionTermination::StopRoot);
                if (!ct::expect((*results)[1].reports.size() == 1)) {
                    return;
                }
                ct::expect(
                    interpreter_diagnostic_code((*results)[1].reports[0].reason())
                    == DiagnosticCode::InterpretExecution
                );
                ct::expect(
                    execution_message((*results)[1].reports[0]).contains("explicit failure")
                );
                ct::expect((*results)[1].termination == ExecutionTermination::StopRoot);
                ct::expect((*results)[2].reports.empty());
                ct::expect((*results)[2].termination == ExecutionTermination::Continue);
                ct::expect(output == "next\n");
                ct::expect(program.constants().size() == constants);
                ct::expect(program.types().size() == types);
            }
        }
    );

    ct::test(
        "Interpreter: runtime tests admit executed paths and continue after unsupported calls",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        private import(cpp) fn native();
        fn helper(selected: bool) { if selected { native(); } }
        test "skipped" { helper(false); println("skipped"); }
        test "reached" { helper(true); println("unreachable"); }
        test "later" { println("later"); }
    )");
            auto output = std::string();
            const auto results = interpret_tests(
                program,
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    output.append(bytes);
                },
                InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
            );
            if (!ct::expect(results.has_value())) {
                return;
            }
            if (!ct::expect(results->size() == 3)) {
                return;
            }
            ct::expect((*results)[0].reports.empty());
            if (!ct::expect((*results)[1].reports.size() == 1)) {
                return;
            }
            ct::expect(
                interpreter_diagnostic_code((*results)[1].reports[0].reason())
                == DiagnosticCode::InterpretAdmission
            );
            ct::expect((*results)[1].reports[0].termination() == ExecutionTermination::StopRoot);
            ct::expect((*results)[1].termination == ExecutionTermination::StopRoot);
            ct::expect(!(*results)[1].aborted());
            ct::expect((*results)[2].reports.empty());
            ct::expect(output == "skipped\nlater\n");
        }
    );

    ct::test(
        "Interpreter: each runtime test receives an independent execution budget",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        test "limited" { while true {} }
        test "fresh" { check(true); println("fresh"); }
    )");
            auto limits = static_execution_limits();
            limits.steps = 20uz;
            auto output = std::string();
            const auto results = interpret_tests(
                program,
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    output.append(bytes);
                },
                InterpreterOptions {.limits = limits, .trace = {}, .report = {}}
            );
            if (!ct::expect(results.has_value())) {
                return;
            }
            if (!ct::expect(results->size() == 2)) {
                return;
            }
            if (!ct::expect((*results)[0].reports.size() == 1)) {
                return;
            }
            ct::expect(
                interpreter_diagnostic_code((*results)[0].reports[0].reason())
                == DiagnosticCode::InterpretLimit
            );
            ct::expect(!(*results)[0].aborted());
            ct::expect((*results)[0].reports[0].termination() == ExecutionTermination::StopRoot);
            ct::expect((*results)[0].termination == ExecutionTermination::StopRoot);
            ct::expect((*results)[1].reports.empty());
            ct::expect(output == "fresh\n");
        }
    );

    ct::test(
        "Interpreter: fatal assertions retain earlier diagnostics and stop remaining tests",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        test "earlier" { println("before"); check(false, "first"); println("after"); }
        test "fatal" { check(false, "second"); assert(false, "fatal"); }
        test "later" { println("unreachable"); }
    )");
            auto events = std::string();
            const auto result = interpret_tests(
                program,
                [&](ExecutionOutputStream, std::string_view bytes) noexcept { events += bytes; },
                InterpreterOptions {
                    .limits = static_execution_limits(),
                    .trace = {},
                    .report = [&](std::optional<TestID> test,
                                  const ExecutionEvent& event) noexcept {
                        ct::expect(test.has_value());
                        if (!ct::expect(event.report_kind().has_value())) {
                            return;
                        }
                        events +=
                            *event.report_kind() == ReportKind::Assert ? "assert\n" : "check\n";
                    }
                }
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            if (!ct::expect(result->size() == 2)) {
                return;
            }
            ct::expect(!(*result)[0].aborted());
            ct::expect((*result)[0].termination == ExecutionTermination::Continue);
            if (!ct::expect((*result)[0].reports.size() == 1)) {
                return;
            }
            ct::expect(execution_message((*result)[0].reports[0]).contains("first"));
            ct::expect((*result)[1].aborted());
            if (!ct::expect((*result)[1].reports.size() == 2)) {
                return;
            }
            ct::expect(execution_message((*result)[1].reports[0]).contains("second"));
            ct::expect(execution_message((*result)[1].reports[1]).contains("fatal"));
            ct::expect(events == "before\ncheck\nafter\ncheck\nassert\n");
        }
    );

    ct::test(
        "Interpreter: runtime traps retain prior checks and abort remaining tests",
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::string_view declaration;
                std::string_view operation;
                std::string_view message;
            };
            const auto inputs = std::array {
                Input {
                    .name = "division",
                    .declaration = "fn divide(value: i32) { println(4 / value); }",
                    .operation = "divide(0);",
                    .message = "division by zero"
                },
                Input {
                    .name = "remainder",
                    .declaration = "fn remainder(value: i32) { println(4 % value); }",
                    .operation = "remainder(0);",
                    .message = "division by zero"
                },
                Input {
                    .name = "shift",
                    .declaration = "fn shift(count: i32) { println(1 << count); }",
                    .operation = "shift(-1);",
                    .message = "shift count is outside"
                },
                Input {
                    .name = "array index",
                    .declaration =
                        "fn element(values: [i32; 1], index: i32) { println(values[index]); }",
                    .operation = "element([1], -1);",
                    .message = "sequence index is out of bounds"
                },
                Input {
                    .name = "slice index",
                    .declaration =
                        "fn element(values: [i32], index: i32) { println(values[index]); }",
                    .operation = "element([1], 1);",
                    .message = "sequence index is out of bounds"
                },
                Input {
                    .name = "slice range",
                    .declaration =
                        "fn slice(values: [i32], start: usize, end: usize) { println(values.slice(start, end).len()); }",
                    .operation = "slice([1, 2], 2, 1);",
                    .message = "slice range is out of bounds"
                },
                Input {
                    .name = "SIMD lane",
                    .declaration =
                        "fn lane(value: u8x16, index: usize) { println(value.lane(index)); }",
                    .operation = "lane(u8x16::splat(7), 16);",
                    .message = "SIMD index or memory range is out of bounds"
                },
                Input {
                    .name = "SIMD load",
                    .declaration =
                        "fn load(values: [u8], index: usize) { let _ = u8x16::load(values, index); }",
                    .operation = "load([1u8], 0);",
                    .message = "SIMD index or memory range is out of bounds"
                },
                Input {
                    .name = "unchecked scalar precondition",
                    .declaration =
                        "fn character(value: u32) { let _ = char::from_u32_unchecked(value); }",
                    .operation = "character(0xd800);",
                    .message = "requires a Unicode scalar value"
                },
            };
            ct::each(inputs, &Input::name, [](const Input& input) static noexcept {
                auto source = std::string(input.declaration);
                source += R"(
                    test "fatal" { println("before"); check(false, "earlier");
                )";
                source += input.operation;
                source += R"(
                        println("unreachable");
                    }
                    test "later" { println("unreachable"); }
                )";
                const auto program = analyze_test_program(source);
                auto output = std::string();
                const auto results = interpret_tests(
                    program,
                    [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                        output.append(bytes);
                    },
                    InterpreterOptions {
                        .limits = static_execution_limits(),
                        .trace = {},
                        .report = {}
                    }
                );
                if (!ct::expect(results.has_value()) || !ct::expect(results->size() == 1)) {
                    return;
                }
                const auto& result = results->front();
                ct::expect(result.aborted());
                if (!ct::expect(result.reports.size() == 2)) {
                    return;
                }
                ct::expect(result.reports.front().termination() == ExecutionTermination::Continue);
                ct::expect(execution_message(result.reports.front()).contains("earlier"));
                ct::expect(result.reports.back().termination() == ExecutionTermination::Abort);
                ct::expect(execution_message(result.reports.back()).contains(input.message));
                ct::expect_equal(output, "before\n");
            });
        }
    );

    ct::test(
        "Interpreter: unchecked character construction checks the executed precondition",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        fn character(value: u32) -> char => char::from_u32_unchecked(value);
        println(character(0x10ffff) as u32);
        if false { println(character(0xd800)); }
        println(character(0xd800));
        println("unreachable");
    )");
            auto output = std::string();
            const auto result = interpret(
                program,
                entry(program),
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    output.append(bytes);
                },
                InterpreterOptions {.limits = static_execution_limits(), .trace = {}, .report = {}}
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect(
                interpreter_diagnostic_code(result.error().event.reason())
                == DiagnosticCode::InterpretExecution
            );
            ct::expect(result.error().event.termination() == ExecutionTermination::Abort);
            ct::expect(output == "1114111\n");
        }
    );
});

} // namespace
