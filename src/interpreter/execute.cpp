module carven:interpreter.execute.impl;

import :diagnostics.code;
import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.evaluation.executor;
import :semantic.evaluation.standard;
import :semantic.evaluation.value;
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

} // namespace

class InterpreterContext final : public SemanticExecutionContext {
public:
    InterpreterContext(
        const SemIRProgram& program,
        ExecutionOutput output,
        const InterpreterOptions& options,
        HostEffects host_effects
    ) noexcept;

    auto stage() const noexcept -> ExecutionStage override;
    auto host_effects_rejection() const noexcept -> std::optional<std::string_view> override;
    auto run(FunctionID entry) noexcept -> std::expected<void, ExecutionHalt>;
    auto run_tests() noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt>;

    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;

    auto prepare_call(ExecutionCallSite target, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionCallTarget, ExecutionCallFailure>> override;

    auto operator()(
        ExecutionCallSite target,
        std::span<ExecutionValue> arguments,
        ProgramOriginID origin
    ) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;

    auto report(const ExecutionEvent& event) noexcept -> void override;
    auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void override;
    auto trace(const ExecutionTraceEvent& event) noexcept -> void override;

private:
    auto prepare_target(ExecutionCallSite target, ProgramOriginID origin) noexcept
        -> std::expected<ExecutionCallTarget, ExecutionCallFailure>;

    auto reject(ProgramOriginID origin, std::string_view message) noexcept -> ExecutionHalt;

    const SemIRProgram& program;
    ExecutionOutput output;
    const InterpreterOptions& options;
    PublishedConstantValues values;
    HostEffects host_effects;
    std::vector<InterpreterTestResult> test_results;
};

InterpreterContext::InterpreterContext(
    const SemIRProgram& program,
    ExecutionOutput output,
    const InterpreterOptions& options,
    HostEffects host_effects
) noexcept
    : program(program),
      output(output),
      options(options),
      values(program),
      host_effects(host_effects) {}

auto InterpreterContext::stage() const noexcept -> ExecutionStage {
    return ExecutionStage::Interpreted;
}

auto InterpreterContext::host_effects_rejection() const noexcept
    -> std::optional<std::string_view> {
    return host_effects == HostEffects::Deny
        ? std::optional<std::string_view>("host effects are disabled for this call")
        : std::nullopt;
}

auto InterpreterContext::reject(ProgramOriginID origin, std::string_view message) noexcept
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
        .blocks = {},
    };
    report(event);
    return {.event = std::move(event)};
}

auto InterpreterContext::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    return program.source_function(callable);
}

auto InterpreterContext::prepare_call(ExecutionCallSite target, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionCallTarget, ExecutionCallFailure>> {
    return ContinuationTask<std::expected<ExecutionCallTarget, ExecutionCallFailure>>(
        prepare_target(target, origin)
    );
}

auto InterpreterContext::prepare_target(ExecutionCallSite target, ProgramOriginID origin) noexcept
    -> std::expected<ExecutionCallTarget, ExecutionCallFailure> {
    if (std::holds_alternative<const SemanticExpression*>(target)) {
        return ExecutionHostCall {.target = target, .invoke = *this, .asynchronous = false};
    }
    const auto* selected = std::get_if<CallableID>(&target);
    if (selected == nullptr) {
        invariant_violation("host call has no callable or native expression");
    }
    const auto callable = *selected;
    const auto& declaration = program.declarations().callable(callable);
    if (const auto* parser = std::get_if<NumberParseImplementation>(&declaration.implementation)) {
        return ExecutionBuiltinCall {
            .result = program.callable_signatures().signature(declaration.signature).result,
            .implementation = *parser
        };
    }
    if (const auto* provided =
            std::get_if<ProvidedFunctionImplementation>(&declaration.implementation)) {
        return ExecutionBuiltinCall {
            .result = program.callable_signatures().signature(declaration.signature).result,
            .implementation = *provided
        };
    }
    if (options.host_call
        && std::holds_alternative<CppImportImplementation>(declaration.implementation)) {
        return ExecutionHostCall {
            .target = callable,
            .invoke = options.host_call,
            .asynchronous = program.callable_signatures().signature(declaration.signature).execution
                == CallableExecutionKind::Async
        };
    }
    const auto body = callable_body_id(declaration);
    if (!body) {
        return std::unexpected(
            ExecutionEvent {
                .origin = origin,
                .cause =
                    ExecutionIssue {
                        .reason = ExecutionReason::Admission,
                        .message = std::holds_alternative<CppImportImplementation>(
                                       declaration.implementation
                                   )
                            ? "native C++ function requires compiled execution"
                            : "callable has no executable interpreter body",
                        .termination = ExecutionTermination::StopRoot,
                    },
                .fields = {},
                .calls = {},
                .blocks = {},
            }
        );
    }
    const auto& metadata = program.bodies().body(*body);
    return ExecutionBody(metadata);
}

auto InterpreterContext::operator()(
    ExecutionCallSite target,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    if (const auto* expression = std::get_if<const SemanticExpression*>(&target)) {
        if (auto result = standard_execution_call(values, *this, **expression, arguments, origin)) {
            return std::move(*result);
        }
    }
    if (options.host_call) {
        return options.host_call(target, arguments, origin);
    }
    return std::unexpected(
        ExecutionEvent {
            .origin = origin,
            .cause =
                ExecutionIssue {
                    .reason = ExecutionReason::Admission,
                    .message = "native C++ operation has no compiler implementation",
                    .termination = ExecutionTermination::StopRoot,
                },
            .fields = {},
            .calls = {},
            .blocks = {},
        }
    );
}

auto InterpreterContext::report(const ExecutionEvent& event) noexcept -> void {
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

auto InterpreterContext::write(ExecutionOutputStream stream, std::string_view bytes) noexcept
    -> void {
    if (output) {
        output(stream, bytes);
    }
}

auto InterpreterContext::trace(const ExecutionTraceEvent& event) noexcept -> void {
    if (options.trace) {
        options.trace(event);
    }
}

auto InterpreterContext::run(FunctionID entry) noexcept -> std::expected<void, ExecutionHalt> {
    const auto& declaration = program.declarations().function(entry);
    const auto& signature = program.callable_signatures().signature(
        program.declarations().callable(declaration.callable).signature
    );
    if (!signature.parameters.empty()) {
        return std::unexpected(
            reject(declaration.origin, "an interpreted root requires a parameterless function")
        );
    }
    auto result =
        execute_function(values, *this, declaration.callable, declaration.origin, options.limits)
            .run();
    if (!result) {
        return std::unexpected(interpreter_halt(std::move(result.error())));
    }
    return {};
}

auto InterpreterContext::run_tests() noexcept
    -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt> {
    auto selected = std::vector<TestID>();
    for (const auto row : program.tests().entries()) {
        if (row.value.execution != TestExecution::Static) {
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
    return InterpreterContext(program, output, options, HostEffects::Allow).run(entry);
}

auto interpret_tests(
    const SemIRProgram& program,
    ExecutionOutput output,
    const InterpreterOptions& options
) noexcept -> std::expected<std::vector<InterpreterTestResult>, ExecutionHalt> {
    return InterpreterContext(program, output, options, HostEffects::Allow).run_tests();
}

struct InterpreterInvocationState final {
    InterpreterOptions options;
    PublishedConstantValues values;
    InterpreterContext context;
    SemanticExecutor executor;
    const SemIRProgram& program;
    FunctionID function;

    InterpreterInvocationState(
        const SemIRProgram& program,
        FunctionID function,
        std::vector<ExecutionValue> arguments,
        ExecutionOutput output,
        InterpreterOptions options,
        HostEffects host_effects
    ) noexcept;
};

InterpreterInvocationState::InterpreterInvocationState(
    const SemIRProgram& program,
    FunctionID function,
    std::vector<ExecutionValue> arguments,
    ExecutionOutput output,
    InterpreterOptions options,
    HostEffects host_effects
) noexcept
    : options(options),
      values(program),
      context(program, output, this->options, host_effects),
      executor(values, context, options.limits),
      program(program),
      function(function) {
    auto inputs = std::vector<ExecutionOperand>();
    for (auto& argument : arguments) {
        inputs.emplace_back(std::move(argument));
    }
    const auto& declaration = program.declarations().function(function);
    executor.begin_call(declaration.callable, std::move(inputs), declaration.origin);
}

InterpreterInvocation::InterpreterInvocation(
    const SemIRProgram& program,
    FunctionID function,
    std::vector<ExecutionValue> arguments,
    ExecutionOutput output,
    InterpreterOptions options,
    HostEffects host_effects
) noexcept
    : state(
          std::make_unique<InterpreterInvocationState>(
              program,
              function,
              std::move(arguments),
              output,
              options,
              host_effects
          )
      ) {}

InterpreterInvocation::~InterpreterInvocation() noexcept = default;

auto InterpreterInvocation::advance(std::size_t quantum) noexcept -> ExecutionProgress {
    return state->executor.advance(quantum);
}

auto InterpreterInvocation::run() noexcept -> ExecutionResult<ExecutionValue> {
    state->executor.run_until_complete(
        state->program.declarations().function(state->function).origin
    );
    return take_result();
}

auto InterpreterInvocation::cancel() noexcept -> void {
    state->executor.cancel();
}

auto InterpreterInvocation::take_result() noexcept -> ExecutionResult<ExecutionValue> {
    auto result = state->executor.take_result();
    if (result) {
        return state->executor.detach_result(
            std::move(*result),
            state->program.declarations().function(state->function).origin
        );
    }
    return result;
}
