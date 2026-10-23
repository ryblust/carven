module carven:interpreter.execute;

import :diagnostics.code;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.program;
import :support.function_ref;
import std;

struct InterpreterOptions final {
    ExecutionLimits limits;
    FunctionRef<void(const ExecutionTraceEvent&) noexcept> trace;
    FunctionRef<void(std::optional<TestID>, const ExecutionEvent&) noexcept> report;
    ExecutionNativeCall host_call;
};

// Executes a parameterless source function. The caller selects the function;
// ordinary program commands select its declared entry.
// The published program and callback recipients outlive synchronous execution.
// The report callback receives execution events as they occur; results retain them.
auto interpret(
    const SemIRProgram& program,
    FunctionID entry,
    ExecutionOutput output,
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

// Runtime tests execute in canonical module and source order. Each receives
// fresh storage and an independent execution budget.
// A trap or fatal assertion ends the returned prefix with an aborted completion.
auto interpret_tests(
    const SemIRProgram& program,
    ExecutionOutput output,
    const InterpreterOptions& options
) noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt>;

struct InterpreterInvocationState;

// The program and callback recipients outlive the invocation, including pauses.
class InterpreterInvocation final {
public:
    InterpreterInvocation(
        const SemIRProgram& program,
        FunctionID function,
        std::vector<ExecutionValue> arguments,
        ExecutionOutput output,
        InterpreterOptions options,
        HostEffects host_effects
    ) noexcept;

    InterpreterInvocation(const InterpreterInvocation&) = delete;
    InterpreterInvocation(InterpreterInvocation&&) = delete;
    auto operator=(const InterpreterInvocation&) -> InterpreterInvocation& = delete;
    auto operator=(InterpreterInvocation&&) -> InterpreterInvocation& = delete;
    ~InterpreterInvocation() noexcept;
    auto advance(std::size_t quantum) noexcept -> ExecutionProgress;
    auto run() noexcept -> ExecutionResult<ExecutionValue>;
    auto cancel() noexcept -> void;
    auto take_result() noexcept -> ExecutionResult<ExecutionValue>;

private:
    std::unique_ptr<InterpreterInvocationState> state;
};
