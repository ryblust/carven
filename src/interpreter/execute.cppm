module carven:interpreter.execute;

import :semantic.evaluation.execution;
import :semantic.semir.program;
import std;

struct InterpreterOptions final {
    ExecutionLimits limits;
    std::function<void(const ExecutionTraceEvent&)> trace;
};

// The published program and callback recipients outlive synchronous execution.
auto interpret(
    const SemIRProgram& program,
    FunctionID entry,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<void, ExecutionDiagnostic>;

struct InterpreterTestResult final {
    TestID test;
    std::vector<ExecutionDiagnostic> diagnostics;
};

// Admission covers every runtime test before any runtime body executes.
// Each test receives fresh storage and an independent execution budget.
auto interpret_tests(
    const SemIRProgram& program,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionDiagnostic>;
