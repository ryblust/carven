module carven:interpreter.execute;

import :diagnostics.code;
import :semantic.evaluation.execution;
import :semantic.semir.program;
import std;

struct InterpreterOptions final {
    ExecutionLimits limits;
    std::function<void(const ExecutionTraceEvent&)> trace;
    std::function<void(std::optional<TestID>, const ExecutionEvent&)> report;
};

// The published program and callback recipients outlive synchronous execution.
// The report callback receives execution events as they occur; results retain them.
auto interpret(
    const SemIRProgram& program,
    FunctionID entry,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<void, ExecutionHalt>;

auto interpreter_diagnostic_code(ExecutionReason reason) noexcept -> DiagnosticCode;

struct InterpreterTestResult final {
    TestID test;
    std::vector<ExecutionEvent> reports;
    // Comes from the execution result; reports alone do not stop a test.
    ExecutionTermination termination;
    auto aborted() const noexcept -> bool;
};

// Admission covers every runtime test before any runtime body executes.
// Each test receives fresh storage and an independent execution budget.
// A trap or fatal assertion ends the returned prefix with an aborted completion.
auto interpret_tests(
    const SemIRProgram& program,
    const ExecutionOutput& output,
    const InterpreterOptions& options
) noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt>;
