module carven:interpreter.execute.impl;

import :diagnostics.code;
import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :support.invariant;
import std;

namespace {

class Interpreter final : public SemanticExecutionContext {
public:
    Interpreter(
        const SemIRProgram& program,
        const ExecutionOutput& output,
        const InterpreterOptions& options
    ) noexcept;
    auto run(FunctionID entry) noexcept -> std::expected<void, ExecutionDiagnostic>;
    auto run_tests() noexcept
        -> std::expected<std::vector<InterpreterTestResult>, ExecutionDiagnostic>;
    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;
    auto prepare_call(FunctionID function, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> override;
    auto report(const ExecutionDiagnostic& diagnostic) noexcept -> void override;
    auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void override;
    auto trace(const ExecutionTraceEvent& event) noexcept -> void override;

private:
    auto admit_modules() noexcept -> void;
    auto reject(ProgramOriginID origin, std::string_view message) noexcept -> void;

    const SemIRProgram& program;
    const ExecutionOutput& output;
    const InterpreterOptions& options;
    PublishedConstantValues values;
    std::map<CallableID, FunctionID> functions;
    std::optional<ExecutionDiagnostic> error;
    bool testing = false;
    std::vector<InterpreterTestResult> test_results;
};

Interpreter::Interpreter(
    const SemIRProgram& program,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept
    : program(program),
      output(output),
      options(options),
      values(program) {
    for (const auto row : program.declarations().functions()) {
        functions.emplace(row.value.callable, row.id);
    }
}

auto Interpreter::reject(ProgramOriginID origin, std::string_view message) noexcept -> void {
    if (!error) {
        error = ExecutionDiagnostic {
            .origin = origin,
            .code = DiagnosticCode::InterpretAdmission,
            .message = std::string(message),
            .calls = {},
            .report_kind = std::nullopt
        };
        if (options.report) {
            options.report(std::nullopt, *error);
        }
    }
}

auto Interpreter::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    const auto found = functions.find(callable);
    return found == functions.end() ? std::nullopt : std::optional(found->second);
}

auto Interpreter::prepare_call(FunctionID function, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> {
    const auto& declaration = program.declarations().function(function);
    const auto& callable = program.declarations().callable(declaration.callable);
    const auto body = callable_body_id(callable);
    if (!body) {
        co_return std::unexpected(
            ExecutionDiagnostic {
                .origin = origin,
                .code = DiagnosticCode::InterpretAdmission,
                .message = "native function has no interpreter implementation",
                .calls = {},
                .report_kind = std::nullopt
            }
        );
    }
    co_return ExecutionBody(program.bodies().body(*body));
}

auto Interpreter::report(const ExecutionDiagnostic& diagnostic) noexcept -> void {
    auto reported = diagnostic;
    if (diagnostic.code == DiagnosticCode::ConstLimit) {
        reported.code = DiagnosticCode::InterpretLimit;
    } else if (diagnostic.code == DiagnosticCode::ConstAdmission
               || diagnostic.code == DiagnosticCode::InterpretAdmission) {
        reported.code = DiagnosticCode::InterpretAdmission;
    } else if (diagnostic.code != DiagnosticCode::AssertionFailed) {
        reported.code = DiagnosticCode::InterpretExecution;
    }
    if (options.report) {
        options.report(testing ? std::optional(test_results.back().test) : std::nullopt, reported);
    }
    if (testing) {
        test_results.back().diagnostics.push_back(std::move(reported));
    } else if (!error) {
        error = std::move(reported);
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

auto Interpreter::admit_modules() noexcept -> void {
    for (const auto module_declaration : program.declarations().modules()) {
        if (!module_declaration.value.cpp_source_fragments.empty()) {
            reject(
                module_declaration.value.origin,
                "native source fragments may initialize state and require a native runtime"
            );
        }
    }
}

auto Interpreter::run(FunctionID entry) noexcept -> std::expected<void, ExecutionDiagnostic> {
    admit_modules();
    const auto& declaration = program.declarations().function(entry);
    if (declaration.entry_point != EntryPointKind::NoArguments) {
        reject(
            declaration.origin,
            "command-line argument values are not supported by the interpreter"
        );
    }
    if (error) {
        return std::unexpected(std::move(*error));
    }
    const auto result =
        execute_function(values, *this, entry, declaration.origin, options.limits).run();
    if (error) {
        return std::unexpected(std::move(*error));
    }
    if (!result) {
        invariant_violation("interpreter execution failed without a diagnostic");
    }
    return {};
}

auto Interpreter::run_tests() noexcept
    -> std::expected<std::vector<InterpreterTestResult>, ExecutionDiagnostic> {
    testing = true;
    admit_modules();
    auto selected = std::vector<TestID>();
    for (const auto row : program.tests().entries()) {
        if (!row.value.is_const) {
            selected.push_back(row.id);
        }
    }
    std::ranges::stable_sort(selected, [&](TestID left, TestID right) noexcept {
        const auto module_name = [&](TestID id) noexcept {
            const auto& module =
                program.declarations().module_decl(program.tests().test(id).module_id);
            return program.provenance().module_record(module.provenance_module).path.value();
        };
        return module_name(left) < module_name(right);
    });
    if (error) {
        return std::unexpected(std::move(*error));
    }
    for (const auto id : selected) {
        test_results.push_back({.test = id, .diagnostics = {}});
        const auto result = execute_body(
                                values,
                                *this,
                                ExecutionBody(program.bodies().body(program.tests().test(id).body)),
                                options.limits
        )
                                .run();
        if (!result && test_results.back().diagnostics.empty()) {
            invariant_violation("interpreted test failed without a diagnostic");
        }
        if (test_results.back().aborted()) {
            break;
        }
    }
    return std::move(test_results);
}

} // namespace

auto InterpreterTestResult::aborted() const noexcept -> bool {
    return !diagnostics.empty() && diagnostics.back().code == DiagnosticCode::AssertionFailed;
}

auto interpret(
    const SemIRProgram& program,
    FunctionID entry,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<void, ExecutionDiagnostic> {
    return Interpreter(program, output, options).run(entry);
}

auto interpret_tests(
    const SemIRProgram& program,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionDiagnostic> {
    return Interpreter(program, output, options).run_tests();
}
