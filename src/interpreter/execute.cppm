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
