module carven:semantic.evaluation.execution.impl;

import :semantic.evaluation.execution;
import :semantic.evaluation.executor;
import :semantic.semir.traversal;
import :support.invariant;
import std;

auto ExecutionEvent::reason() const noexcept -> ExecutionReason {
    if (const auto* issue = std::get_if<ExecutionIssue>(&cause)) {
        return issue->reason;
    }
    return report_kind() == ReportKind::Assert ? ExecutionReason::Assertion : ExecutionReason::Test;
}

auto ExecutionEvent::message() const noexcept -> std::string_view {
    if (const auto* issue = std::get_if<ExecutionIssue>(&cause)) {
        return issue->message;
    }
    switch (*report_kind()) {
        case ReportKind::Assert:  return "assertion failed";
        case ReportKind::Check:   return "check failed";
        case ReportKind::Require: return "requirement failed";
        case ReportKind::Fail:    return "explicit failure";
    }
    std::unreachable();
}

auto ExecutionEvent::report_kind() const noexcept -> std::optional<ReportKind> {
    if (const auto* report = std::get_if<ReportKind>(&cause)) {
        return *report;
    }
    return std::nullopt;
}

auto ExecutionEvent::termination() const noexcept -> ExecutionTermination {
    if (const auto* issue = std::get_if<ExecutionIssue>(&cause)) {
        if (issue->termination == ExecutionTermination::Continue) {
            invariant_violation("an execution issue cannot continue");
        }
        return issue->termination;
    }
    switch (*report_kind()) {
        case ReportKind::Check:   return ExecutionTermination::Continue;
        case ReportKind::Assert:  return ExecutionTermination::Abort;
        case ReportKind::Require:
        case ReportKind::Fail:    return ExecutionTermination::StopRoot;
    }
    std::unreachable();
}

auto SemanticExecutionContext::trace(const ExecutionTraceEvent&) noexcept -> void {}

auto SemanticExecutionContext::bind_call(
    CallableID callable,
    std::vector<ExecutionOperand>&,
    ExecutionArgumentAccess&,
    ProgramOriginID
) noexcept -> ContinuationTask<std::expected<CallableID, ExecutionCallFailure>> {
    co_return callable;
}

auto SemanticExecutionContext::freeze(ExecutionValue) noexcept -> std::optional<ConstantID> {
    invariant_violation("this execution context has no static stage");
}

ExecutionBody::ExecutionBody(const StructuredBodyDraft& body) noexcept
    : ExecutionBody(body, body.residual ? *body.residual : body.region) {}

ExecutionBody::ExecutionBody(const SemIRBody& body) noexcept
    : ExecutionBody(body, body.region()) {}

ExecutionBody::ExecutionBody(const StructuredBodyDraft& body, const SemanticRegion& region) noexcept
    : body(&body),
      selected_region(&region) {
    index_children();
}

ExecutionBody::ExecutionBody(const SemIRBody& body, const SemanticRegion& region) noexcept
    : body(&body),
      selected_region(&region) {
    index_children();
}

auto ExecutionBody::kind() const noexcept -> BodyKind {
    if (const auto* draft = std::get_if<const StructuredBodyDraft*>(&body)) {
        return (*draft)->kind;
    }
    return std::get<const SemIRBody*>(body)->kind();
}

auto ExecutionBody::region() const noexcept -> const SemanticRegion& {
    return *selected_region;
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
auto finish_execution(SemanticExecutor& executor, ExecutionResult<Value> result) noexcept
    -> ExecutionResult<Value> {
    if (!result) {
        return std::unexpected(executor.escaped(std::move(result.error())));
    }
    return result;
}

} // namespace

auto execute_static_root(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const SemanticExpression& expression,
    ExecutionLimits limits
) noexcept -> ExecutionTask<ExecutionValue> {
    auto executor = SemanticExecutor(values, context, limits);
    co_return finish_execution(executor, (co_await executor.evaluate_root(expression)));
}

auto execute_body(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    ExecutionBody body,
    ExecutionLimits limits
) noexcept -> ExecutionTask<void> {
    auto executor = SemanticExecutor(values, context, limits);
    co_return finish_execution(executor, (co_await executor.evaluate_body(body)));
}

auto execute_function(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    CallableID callable,
    ProgramOriginID origin,
    ExecutionLimits limits
) noexcept -> ExecutionTask<ExecutionValue> {
    auto executor = SemanticExecutor(values, context, limits);
    auto result = (co_await executor.invoke(callable, {}, origin));
    if (result) {
        result = executor.detach_result(std::move(*result), origin);
    }
    co_return finish_execution(executor, std::move(result));
}

auto ExecutionBody::binding_lifetime(LocalBindingID binding) const noexcept -> LifetimeRegionID {
    return body.visit([&](const auto* source) noexcept {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(*source)>, StructuredBodyDraft>) {
            return source->bindings.get(binding).lifetime;
        } else {
            return source->binding(binding).lifetime;
        }
    });
}

auto ExecutionBody::index_children() noexcept -> void {
    visit_semantic_nodes(*selected_region, [&](const auto& node) noexcept {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(node)>, SemanticStatement>) {
            if (node.reachable) {
                if (const auto* child = std::get_if<SemAsyncLet>(&node.value)) {
                    child_lifetimes.push_back(binding_lifetime(child->child));
                }
            }
        }
    });
    std::ranges::sort(child_lifetimes);
    const auto redundant = std::ranges::unique(child_lifetimes);
    child_lifetimes.erase(redundant.begin(), redundant.end());
}

auto ExecutionBody::has_children(LifetimeRegionID lifetime) const noexcept -> bool {
    return std::ranges::binary_search(child_lifetimes, lifetime);
}
