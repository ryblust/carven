module carven:interpreter.execute;

import :semantic.evaluation.execution;
import :semantic.semir.program;
import std;

struct InterpreterOptions final {
    ExecutionLimits limits;
    std::function<void(const ExecutionTraceEvent&)> trace;
    std::function<void(std::optional<TestID>, const ExecutionDiagnostic&)> report;
};

// The published program and callback recipients outlive synchronous execution.
// The report callback receives diagnostics as they occur; results retain them.
auto interpret(
    const SemIRProgram& program,
    FunctionID entry,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<void, ExecutionDiagnostic>;

struct InterpreterTestResult final {
    TestID test;
    std::vector<ExecutionDiagnostic> diagnostics;
    auto aborted() const noexcept -> bool;
};

// Admission covers every runtime test before any runtime body executes.
// Each test receives fresh storage and an independent execution budget.
// A fatal assertion ends the returned prefix; its diagnostic records the abort.
auto interpret_tests(
    const SemIRProgram& program,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionDiagnostic>;
