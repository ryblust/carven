module carven:interpreter.execute.impl;

import :diagnostics.code;
import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :support.invariant;
import std;

namespace {

auto interpreter_halt(ExecutionFailure failure) noexcept -> ExecutionHalt {
    if (auto* halt = std::get_if<ExecutionHalt>(&failure)) {
        return std::move(*halt);
    }
    invariant_violation("interpreter root did not produce its stopping cause");
}

class Interpreter final : public SemanticExecutionContext {
public:
    Interpreter(
        const SemIRProgram& program,
        ExecutionOutput output,
        const InterpreterOptions& options
    ) noexcept;
    auto run(FunctionID entry) noexcept -> std::expected<void, ExecutionHalt>;
    auto run_tests() noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt>;
    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;
    auto prepare_call(CallableID callable, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> override;
    auto report(const ExecutionEvent& event) noexcept -> void override;
    auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void override;
    auto trace(const ExecutionTraceEvent& event) noexcept -> void override;

private:
    auto admit_modules() noexcept -> std::expected<void, ExecutionHalt>;
    auto reject(ProgramOriginID origin, std::string_view message) noexcept -> ExecutionHalt;

    const SemIRProgram& program;
    ExecutionOutput output;
    const InterpreterOptions& options;
    PublishedConstantValues values;
    std::vector<InterpreterTestResult> test_results;
};

Interpreter::Interpreter(
    const SemIRProgram& program,
    ExecutionOutput output,
    const InterpreterOptions& options
) noexcept
    : program(program),
      output(output),
      options(options),
      values(program) {}

auto Interpreter::reject(ProgramOriginID origin, std::string_view message) noexcept
    -> ExecutionHalt {
    auto event = ExecutionEvent {
        .origin = origin,
        .cause =
            ExecutionIssue {
                .reason = ExecutionReason::Admission,
                .message = std::string(message),
                .termination = ExecutionTermination::StopRoot,
            },
        .fields = {},
        .calls = {},
    };
    report(event);
    return {.event = std::move(event)};
}

auto Interpreter::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    return program.source_function(callable);
}

auto Interpreter::prepare_call(CallableID callable, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> {
    const auto& declaration = program.declarations().callable(callable);
    const auto body = callable_body_id(declaration);
    if (!body) {
        co_return std::unexpected(
            ExecutionEvent {
                .origin = origin,
                .cause =
                    ExecutionIssue {
                        .reason = ExecutionReason::Admission,
                        .message = "callable has no executable interpreter body",
                        .termination = ExecutionTermination::StopRoot,
                    },
                .fields = {},
                .calls = {},
            }
        );
    }
    const auto& metadata = program.bodies().body(*body);
    co_return ExecutionBody(metadata);
}

auto Interpreter::report(const ExecutionEvent& event) noexcept -> void {
    // Execution reports while the newest result is active; admission and entry
    // execution have no test result. Report history never decides control.
    if (options.report) {
        options.report(
            test_results.empty() ? std::nullopt : std::optional(test_results.back().test),
            event
        );
    }
    if (!test_results.empty()) {
        test_results.back().reports.push_back(event);
    }
}

auto Interpreter::write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void {
    if (output) {
        output(stream, bytes);
    }
}

auto Interpreter::trace(const ExecutionTraceEvent& event) noexcept -> void {
    if (options.trace) {
        options.trace(event);
    }
}

auto Interpreter::admit_modules() noexcept -> std::expected<void, ExecutionHalt> {
    for (const auto module_declaration : program.declarations().modules()) {
        if (!module_declaration.value.cpp_source_fragments.empty()) {
            return std::unexpected(reject(
                module_declaration.value.origin,
                "native source fragments may initialize state and require a native runtime"
            ));
        }
    }
    return {};
}

auto Interpreter::run(FunctionID entry) noexcept -> std::expected<void, ExecutionHalt> {
    if (auto admitted = admit_modules(); !admitted) {
        return std::unexpected(std::move(admitted.error()));
    }
    const auto& declaration = program.declarations().function(entry);
    if (declaration.entry_point != EntryPointKind::NoArguments) {
        return std::unexpected(reject(
            declaration.origin,
            "command-line argument values are not supported by the interpreter"
        ));
    }
    auto result =
        execute_function(values, *this, declaration.callable, declaration.origin, options.limits)
            .run();
    if (!result) {
        return std::unexpected(interpreter_halt(std::move(result.error())));
    }
    return {};
}

auto Interpreter::run_tests() noexcept
    -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt> {
    if (auto admitted = admit_modules(); !admitted) {
        return std::unexpected(std::move(admitted.error()));
    }
    auto selected = std::vector<TestID>();
    for (const auto row : program.tests().entries()) {
        if (!row.value.is_const) {
            selected.push_back(row.id);
        }
    }
    std::ranges::stable_sort(selected, [&](TestID left, TestID right) noexcept {
        const auto module_name = [&](TestID id) noexcept {
            const auto& declaration =
                program.declarations().module_decl(program.tests().test(id).module_id);
            return program.provenance().module_record(declaration.provenance_module).path.value();
        };
        return module_name(left) < module_name(right);
    });
    for (const auto id : selected) {
        test_results.push_back({
            .test = id,
            .reports = {},
            .termination = ExecutionTermination::Continue,
        });
        auto result = execute_body(
                          values,
                          *this,
                          ExecutionBody(program.bodies().body(*program.tests().test(id).body)),
                          options.limits
        )
                          .run();
        if (!result) {
            const auto halt = interpreter_halt(std::move(result.error()));
            test_results.back().termination = halt.event.termination();
        }
        if (test_results.back().aborted()) {
            break;
        }
    }
    return std::move(test_results);
}

} // namespace

auto InterpreterTestResult::aborted() const noexcept -> bool {
    return termination == ExecutionTermination::Abort;
}

auto interpreter_diagnostic_code(ExecutionReason reason) noexcept -> DiagnosticCode {
    switch (reason) {
        case ExecutionReason::Admission: return DiagnosticCode::InterpretAdmission;
        case ExecutionReason::Limit:     return DiagnosticCode::InterpretLimit;
        case ExecutionReason::Assertion: return DiagnosticCode::AssertionFailed;
        default:                         return DiagnosticCode::InterpretExecution;
    }
}

auto interpret(
    const SemIRProgram& program,
    FunctionID entry,
    ExecutionOutput output,
    const InterpreterOptions& options
) noexcept -> std::expected<void, ExecutionHalt> {
    return Interpreter(program, output, options).run(entry);
}

auto interpret_tests(
    const SemIRProgram& program,
    ExecutionOutput output,
    const InterpreterOptions& options
) noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt> {
    return Interpreter(program, output, options).run_tests();
}
