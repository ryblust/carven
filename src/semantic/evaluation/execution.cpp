module carven:semantic.evaluation.execution.impl;

import :semantic.evaluation.execution;
import :semantic.evaluation.executor;
import std;

auto SemanticExecutionContext::trace(const ExecutionTraceEvent&) noexcept -> void {}

ExecutionBody::ExecutionBody(const StructuredBodyDraft& body) noexcept
    : body(&body) {}

ExecutionBody::ExecutionBody(const SemIRBody& body) noexcept
    : body(&body) {}

auto ExecutionBody::kind() const noexcept -> BodyKind {
    if (const auto* draft = std::get_if<const StructuredBodyDraft*>(&body)) {
        return (*draft)->kind;
    }
    return std::get<const SemIRBody*>(body)->kind();
}

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

auto ExecutionBody::binding_access(LocalBindingID id) const noexcept -> AccessMode {
    const auto storage = body.visit([&](const auto* source) noexcept -> BindingStorage {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(*source)>, StructuredBodyDraft>) {
            return source->bindings.get(id).storage;
        } else {
            return source->binding(id).storage;
        }
    });
    if (const auto* parameter = std::get_if<ParameterBindingStorage>(&storage)) {
        return parameter->access;
    }
    return AccessMode::Read;
}

auto ExecutionBody::bindings_in(LifetimeRegionID lifetime) const noexcept
    -> std::vector<std::size_t> {
    return body.visit([&](const auto* source) noexcept {
        auto result = std::vector<std::size_t>();
        const auto bindings = [&]() noexcept {
            if constexpr (std::same_as<
                              std::remove_cvref_t<decltype(*source)>,
                              StructuredBodyDraft>) {
                return source->bindings.entries();
            } else {
                return source->bindings();
            }
        }();
        for (const auto entry : bindings) {
            if (entry.value.lifetime == lifetime) {
                result.push_back(entry.id.index());
            }
        }
        return result;
    });
}

namespace {

template<typename Value>
auto finish_execution(SemanticExecutionContext& context, ExecutionResult<Value> result) noexcept
    -> ExecutionResult<Value> {
    if (!result) {
        if (const auto* failure = std::get_if<ExecutionSourceFailure>(&result.error())) {
            context.report(
                ExecutionDiagnostic {
                    .origin = failure->origin,
                    .code = DiagnosticCode::ConstEvaluation,
                    .message = "typed failure escaped execution without recovery",
                    .calls = failure->calls,
                    .report_kind = std::nullopt,
                }
            );
            return std::unexpected(ExecutionFailure {});
        }
    }
    return result;
}

} // namespace

auto execute_constant_root(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const SemanticExpression& expression,
    ExecutionLimits limits
) noexcept -> ExecutionTask<ExecutionValue> {
    auto executor = SemanticExecutor(values, context, limits);
    co_return finish_execution(context, (co_await executor.evaluate_root(expression)));
}

auto execute_body(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    ExecutionBody body,
    ExecutionLimits limits
) noexcept -> ExecutionTask<void> {
    auto executor = SemanticExecutor(values, context, limits);
    co_return finish_execution(context, (co_await executor.evaluate_body(body)));
}

auto execute_function(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    FunctionID function,
    ProgramOriginID origin,
    ExecutionLimits limits
) noexcept -> ExecutionTask<ExecutionValue> {
    auto executor = SemanticExecutor(values, context, limits);
    auto result = (co_await executor.invoke(function, {}, origin));
    if (result) {
        result = executor.detach_result(std::move(*result), origin);
    }
    co_return finish_execution(context, std::move(result));
}
