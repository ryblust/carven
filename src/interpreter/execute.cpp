module carven:interpreter.execute.impl;

import :diagnostics.code;
import :interpreter.execute;
import :semantic.evaluation.admission;
import :semantic.evaluation.execution;
import :semantic.evaluation.shape;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
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
        -> ContinuationTask<std::expected<ExecutionCallBody, ExecutionCallFailure>> override;
    auto report(const ExecutionDiagnostic& diagnostic) noexcept -> void override;
    auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void override;
    auto trace(const ExecutionTraceEvent& event) noexcept -> void override;
    auto arithmetic() const noexcept -> IntegerArithmetic override;

private:
    auto admit_body(const SemIRBody& body) noexcept -> void;
    auto admit_modules() noexcept -> void;
    auto admit_pending() noexcept -> void;
    auto admit(FunctionID function) noexcept -> void;
    auto reject(ProgramOriginID origin, std::string_view message) noexcept -> void;
    auto supported(TypeID type, bool allow_void = false) const noexcept -> bool;
    auto expression(const SemanticExpression& expression) noexcept -> void;

    const SemIRProgram& program;
    const ExecutionOutput& output;
    const InterpreterOptions& options;
    PublishedConstantValues values;
    ExecutionTypeShapes shapes;
    std::map<CallableID, FunctionID> functions;
    std::map<FunctionID, std::vector<ConstructionTypeRef>> parameters;
    std::set<const SemanticExpression*> direct_callees;
    std::vector<FunctionID> pending;
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
      values(program),
      shapes(values) {
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

auto Interpreter::supported(TypeID type, bool allow_void) const noexcept -> bool {
    return supported_execution_type(values, shapes, ConstructionTypeRef(type), allow_void);
}

auto Interpreter::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    const auto found = functions.find(callable);
    return found == functions.end() ? std::nullopt : std::optional(found->second);
}

auto Interpreter::admit(FunctionID function) noexcept -> void {
    if (error || parameters.contains(function)) {
        return;
    }
    const auto& declaration = program.declarations().function(function);
    const auto& callable = program.declarations().callable(declaration.callable);
    const auto body_id = callable_body_id(callable);
    if (!body_id) {
        reject(declaration.origin, "native function calls are not supported by the interpreter");
        return;
    }
    const auto& signature = program.callable_signatures().signature(callable.signature);
    for (const auto type : program.failure_sets().failure_set(signature.failures).members) {
        if (!supported(type)) {
            reject(declaration.origin, "failure payload type is not supported by the interpreter");
            return;
        }
    }
    if (!supported(signature.result, true)) {
        reject(declaration.origin, "function result type is not supported by the interpreter");
        return;
    }
    auto types = std::vector<ConstructionTypeRef>();
    for (const auto& parameter : signature.parameters) {
        if (parameter.access == AccessMode::Write || !supported(parameter.type)) {
            reject(
                declaration.origin,
                "function parameter type or Write access is not supported by the interpreter"
            );
            return;
        }
        types.push_back(parameter.type);
    }
    parameters.emplace(function, std::move(types));
    admit_body(program.bodies().body(*body_id));
}

auto Interpreter::admit_body(const SemIRBody& body) noexcept -> void {
    for (const auto binding : body.bindings()) {
        if (!supported(binding.value.type)) {
            reject(binding.value.origin, "local type is not supported by the interpreter");
        }
    }
    for (const auto pattern : body.patterns()) {
        if (!supported(pattern.value.type) || !supported_execution_pattern(pattern.value.value)) {
            reject(pattern.value.origin, "pattern is not supported by the interpreter");
        }
    }
    visit_semantic_nodes(
        body.region(),
        Overloaded {
            [&](const SemanticExpression& source) noexcept { expression(source); },
            [&](const SemanticStatement& source) noexcept {
                if (const auto reason = unsupported_execution_statement(source)) {
                    reject(source.origin, *reason);
                }
            }
        }
    );
}

auto Interpreter::expression(const SemanticExpression& source) noexcept -> void {
    if (error) {
        return;
    }
    if (std::holds_alternative<SemCallable>(source.value)) {
        if (!direct_callees.contains(&source)) {
            reject(source.origin, "callable values are not supported by the interpreter");
        }
        return;
    }
    if (!supported(source.type.resolved(), true)) {
        reject(source.origin, "expression type is not supported by the interpreter");
        return;
    }
    if (const auto reason = unsupported_execution_expression(source)) {
        reject(source.origin, *reason);
        return;
    }
    if (const auto* report = std::get_if<SemReport>(&source.value);
        !testing && report != nullptr && report->kind != ReportKind::Assert) {
        reject(source.origin, "test operations require a test execution context");
        return;
    }
    const auto* call = std::get_if<SemCall>(&source.value);
    if (!call) {
        return;
    }
    const auto* target = std::get_if<SemCallable>(&call->callee->value);
    const auto function = target ? function_for_callable(target->callable) : std::nullopt;
    if (!function) {
        reject(source.origin, "interpreter calls must directly select a Carven function");
        return;
    }
    direct_callees.insert(&*call->callee);
    for (const auto& argument : call->arguments) {
        if (argument.access == AccessMode::Write) {
            reject(
                argument.expression.origin,
                "Write arguments are not supported by the interpreter"
            );
        }
    }
    pending.push_back(*function);
}

auto Interpreter::prepare_call(FunctionID function, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionCallBody, ExecutionCallFailure>> {
    const auto& declaration = program.declarations().function(function);
    const auto body = callable_body_id(program.declarations().callable(declaration.callable));
    if (!body || !parameters.contains(function)) {
        co_return std::unexpected(
            ExecutionDiagnostic {
                .origin = origin,
                .code = DiagnosticCode::InterpretAdmission,
                .message = "function is outside the interpreter execution subset",
                .calls = {},
                .report_kind = std::nullopt
            }
        );
    }
    co_return ExecutionCallBody {
        .body = ExecutionBody(program.bodies().body(*body)),
        .parameter_types = parameters.at(function)
    };
}

auto Interpreter::report(const ExecutionDiagnostic& diagnostic) noexcept -> void {
    auto reported = diagnostic;
    reported.code = diagnostic.code == DiagnosticCode::AssertionFailed
        ? DiagnosticCode::AssertionFailed
        : diagnostic.code == DiagnosticCode::ConstLimit ? DiagnosticCode::InterpretLimit
                                                        : DiagnosticCode::InterpretExecution;
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

auto Interpreter::arithmetic() const noexcept -> IntegerArithmetic {
    return IntegerArithmetic::Wrapping;
}

auto Interpreter::trace(const ExecutionTraceEvent& event) noexcept -> void {
    if (options.trace) {
        options.trace(event);
    }
}

auto Interpreter::admit_modules() noexcept -> void {
    for (const auto module_declaration : program.declarations().modules()) {
        if (!module_declaration.value.cpp_headers.empty()
            || !module_declaration.value.cpp_source_fragments.empty()) {
            reject(
                module_declaration.value.origin,
                "native headers and source fragments are not supported by the interpreter"
            );
        }
    }
}

auto Interpreter::admit_pending() noexcept -> void {
    while (!pending.empty() && !error) {
        const auto next = pending.back();
        pending.pop_back();
        admit(next);
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
    pending.push_back(entry);
    admit_pending();
    if (error) {
        return std::unexpected(std::move(*error));
    }
    const auto result =
        execute_function(values, *this, entry, {}, declaration.origin, options.limits).run();
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
    for (const auto id : selected) {
        admit_body(program.bodies().body(program.tests().test(id).body));
    }
    admit_pending();
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
