module carven:semantic.evaluation.execution.impl;

import :semantic.evaluation.execution;
import :semantic.evaluation.executor;
import std;

auto execute_constant_root(
    ConstantValueAccess& values,
    ConstantExecutionContext& context,
    const SemanticExpression& expression,
    ConstantExecutionLimits limits
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    auto executor = ConstantExecutor(values, context, limits);
    return executor.evaluate_root(expression);
}

auto execute_constant_test(
    ConstantValueAccess& values,
    ConstantExecutionContext& context,
    const StructuredBodyDraft& body,
    ConstantExecutionLimits limits
) noexcept -> ConstantExecutionResult<void> {
    auto executor = ConstantExecutor(values, context, limits);
    return executor.evaluate_test(body);
}
