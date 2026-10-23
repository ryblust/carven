module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.execution.impl;

import :hosting.environment;
import :hosting.execution;
import :hosting.operation;
import :hosting.value;
import :semantic.analysis.program;
import :semantic.evaluation.execution;
import :semantic.evaluation.native.request;
import :semantic.evaluation.value;
import :semantic.semir.contract;
import :semantic.semir.decl;
import :semantic.semir.type;
import std;

namespace {

auto host_provider_rejection(
    const SemanticExecutionContext& context,
    const HostProvider& provider
) noexcept -> std::optional<std::string> {
    if (context.stage() == ExecutionStage::Static && !provider.static_effects) {
        return "host binding is not enabled for static execution";
    }
    return std::nullopt;
}

} // namespace

auto hosting_native_call(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const HostEnvironment& environment,
    HostCallContext& host,
    const ExecutionNativeRequest& request,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    auto selected = hosting_cpp_select(values, environment.providers, request, arguments, origin);
    if (!selected) {
        return std::unexpected(std::move(selected.error()));
    }
    const auto& provider = **selected;
    if (const auto rejection = host_provider_rejection(context, provider)) {
        return hosting_service_error(origin, *rejection);
    }
    return hosting_cpp_call(
        values,
        provider.call,
        provider.context,
        provider.contract,
        request,
        arguments,
        environment.limits,
        origin,
        &host,
        context,
        provider.static_effects == HostServiceEffects::Environment
    );
}

auto hosting_analysis_call(
    ProgramDraft& draft,
    SemanticExecutionContext& context,
    const HostEnvironment& environment,
    HostCallContext& host,
    ExecutionCallSite target,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    const auto close_type = [&](ConstructionTypeRef type) noexcept {
        return draft.try_canonicalize_declared_type(type);
    };
    const auto close_failures = [&](ConstructionFailureRef failures) noexcept {
        return draft.construction_failure_known_members(failures);
    };
    if (const auto* expression = std::get_if<const SemanticExpression*>(&target)) {
        const auto request =
            execution_native_request(draft, **expression, close_type, close_failures);
        if (!request) {
            return hosting_service_error(origin, "native request has no closed execution contract");
        }
        return hosting_native_call(draft, context, environment, host, *request, arguments, origin);
    }
    const auto callable = std::get<CallableID>(target);
    const auto function = draft.function_for_callable(callable);
    if (!function) {
        return hosting_service_error(origin, "native call has no source declaration");
    }
    const auto declaration = draft.function_declaration_copy(*function);
    const auto name = draft.module_path(declaration.module_id) + "."
        + std::string(draft.spelling(declaration.name));
    const auto selected = std::ranges::find(
        environment.providers,
        name,
        [](const HostProvider& provider) static noexcept -> const std::string& {
            return provider.contract.name;
        }
    );
    if (selected == environment.providers.end() || selected->contract.cpp_operation) {
        return hosting_service_error(
            origin,
            "native function has no registered implementation: " + name
        );
    }
    const auto construction = draft.construction_callable_contract_copy(callable);
    const auto result = close_type(construction.result);
    auto parameters = std::vector<CallableParameter>();
    for (const auto& parameter : construction.parameters) {
        const auto type = close_type(parameter.type);
        if (!type) {
            return hosting_service_error(origin, "native parameter has no closed execution type");
        }
        parameters.push_back({.stage = parameter.stage, .access = parameter.access, .type = *type});
    }
    if (!result) {
        return hosting_service_error(origin, "native result has no closed execution type");
    }
    const auto failures = close_failures(construction.failures);
    const auto signature = CallableSignature {
        .execution = construction.execution,
        .parameters = std::move(parameters),
        .result = *result,
        .failures = draft.intern_failure_set(failures)
    };
    if (!hosting_contract_matches(draft, signature, selected->contract)) {
        return hosting_service_error(
            origin,
            "native function does not match its registered contract: " + name
        );
    }
    if (const auto rejection = host_provider_rejection(context, *selected)) {
        return hosting_service_error(origin, *rejection);
    }
    return hosting_invoke(
        draft,
        selected->call,
        selected->contract,
        selected->context,
        &host,
        static_cast<std::size_t>(selected - environment.providers.begin()),
        signature.parameters,
        signature.result,
        failures,
        arguments,
        environment.limits,
        environment.native_types,
        origin,
        context,
        selected->static_effects == HostServiceEffects::Environment
    );
}

auto hosting_execution_call(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const HostEnvironment& environment,
    HostCallContext& host,
    ExecutionCallSite target,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    const auto* expression = std::get_if<const SemanticExpression*>(&target);
    if (expression == nullptr) {
        return hosting_service_error(origin, "native function has no registered implementation");
    }
    const auto close_type = [&](ConstructionTypeRef type) noexcept -> std::optional<TypeID> {
        const auto closed = execution_type_reference(values, type);
        if (const auto* id = std::get_if<TypeID>(&closed)) {
            return *id;
        }
        return std::nullopt;
    };
    const auto close_failures = [&](ConstructionFailureRef failures) noexcept {
        return values.failure_set_copy(std::get<FailureSetID>(failures)).members;
    };
    const auto request = execution_native_request(values, **expression, close_type, close_failures);
    if (!request) {
        return hosting_service_error(origin, "native request has no closed execution contract");
    }
    return hosting_native_call(values, context, environment, host, *request, arguments, origin);
}
