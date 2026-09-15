module carven:semantic.evaluation.execution.impl;

import :semantic.evaluation.execution;
import :semantic.evaluation.executor;
import std;

auto SemanticExecutionContext::arithmetic() const noexcept -> IntegerArithmetic {
    return IntegerArithmetic::Checked;
}

auto SemanticExecutionContext::trace(const ExecutionTraceEvent&) noexcept -> void {}

ExecutionBody::ExecutionBody(const StructuredBodyDraft& body) noexcept
    : body(&body) {}

ExecutionBody::ExecutionBody(const SemIRBody& body) noexcept
    : body(&body) {}

auto ExecutionBody::region() const noexcept -> const SemanticRegion& {
    if (const auto* draft = std::get_if<const StructuredBodyDraft*>(&body)) {
        return (*draft)->region;
    }
    return std::get<const SemIRBody*>(body)->region();
}

auto ExecutionBody::parameters() const noexcept -> std::span<const LocalBindingID> {
    if (const auto* draft = std::get_if<const StructuredBodyDraft*>(&body)) {
        return (*draft)->inputs.parameters;
    }
    return std::get<const SemIRBody*>(body)->inputs().parameters;
}

auto ExecutionBody::binding_count() const noexcept -> std::size_t {
    if (const auto* draft = std::get_if<const StructuredBodyDraft*>(&body)) {
        return (*draft)->bindings.size();
    }
    return std::get<const SemIRBody*>(body)->bindings().size();
}

auto ExecutionBody::binding_type(LocalBindingID id) const noexcept -> ConstructionTypeRef {
    if (const auto* draft = std::get_if<const StructuredBodyDraft*>(&body)) {
        return (*draft)->bindings.get(id).type;
    }
    return ConstructionTypeRef(std::get<const SemIRBody*>(body)->binding(id).type);
}

auto execute_constant_root(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const SemanticExpression& expression,
    ExecutionLimits limits
) noexcept -> ExecutionResult<ExecutionValue> {
    auto executor = SemanticExecutor(values, context, limits);
    return executor.evaluate_root(expression);
}

auto execute_constant_test(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const StructuredBodyDraft& body,
    ExecutionLimits limits
) noexcept -> ExecutionResult<void> {
    auto executor = SemanticExecutor(values, context, limits);
    return executor.evaluate_test(body);
}

auto execute_function(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    FunctionID function,
    std::vector<ExecutionValue> arguments,
    ProgramOriginID origin,
    ExecutionLimits limits
) noexcept -> ExecutionResult<ExecutionValue> {
    auto executor = SemanticExecutor(values, context, limits);
    return executor.invoke(function, std::move(arguments), origin);
}
