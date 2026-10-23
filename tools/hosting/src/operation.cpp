module;

#include <carven/hosting/operation.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.operation.impl;

import :hosting.operation;
import :hosting.value;
import :semantic.evaluation.execution;
import :semantic.evaluation.native.request;
import :semantic.evaluation.value;
import :semantic.semir.delegation;
import :semantic.semir.operation;
import :semantic.semir.type;
import std;

auto hosting_cpp_kind(ExecutionNativeKind kind) noexcept -> HostCppOperationKind {
    switch (kind) {
        case ExecutionNativeKind::Function:   return HostCppOperationKind::Function;
        case ExecutionNativeKind::Method:     return HostCppOperationKind::Method;
        case ExecutionNativeKind::Field:      return HostCppOperationKind::Field;
        case ExecutionNativeKind::Conversion: return HostCppOperationKind::Conversion;
    }
    std::unreachable();
}

auto hosting_cpp_matches(
    const HostCallContract& contract,
    const ExecutionValueAccess& values,
    const ExecutionNativeRequest& request,
    std::span<const ExecutionValue> arguments
) noexcept -> bool {
    if (contract.cpp_operation != hosting_cpp_kind(request.kind)
        || contract.name != request.name
        || contract.parameters.size() != arguments.size()
        || request.parameters.size() != arguments.size()
        || (contract.execution == HostCallExecution::Async)
            != (request.execution == CallableExecutionKind::Async)
        || !hosting_failures_match(
            values,
            request.failures,
            contract.failures,
            contract.native_types
        )) {
        return false;
    }
    for (auto index = 0uz; index < arguments.size(); ++index) {
        const auto& parameter = contract.parameters[index];
        const auto& input = request.parameters[index];
        if ((parameter.access == HostCallAccess::Write && input.access == AccessMode::Read)
            || (parameter.access == HostCallAccess::Take && input.access != AccessMode::Take)) {
            return false;
        }
        if (const auto* native = std::get_if<ExecutionHostObject>(&arguments[index])) {
            if (carven::runtime::hosting::detail::ObjectAccess::resolve(
                    native->reference,
                    parameter.type.describe().native_type
                )
                == nullptr) {
                return false;
            }
        } else if (!hosting_call_type(values, input.type, parameter.type, contract.native_types)) {
            return false;
        }
    }
    return hosting_call_type(values, request.result, contract.result, contract.native_types);
}

auto hosting_cpp_call(
    const ExecutionValueAccess& values,
    HostTypedCall call,
    void* context,
    const HostCallContract& contract,
    const ExecutionNativeRequest& request,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    ProgramOriginID origin,
    HostCallContext* host,
    SemanticExecutionContext& execution_context,
    bool environment_effects
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    if (std::ranges::any_of(request.parameters, [&](const auto& parameter) noexcept {
            return parameter.access == AccessMode::Write
                && !std::holds_alternative<CppTypeValue>(values.type_copy(parameter.type).value);
        })) {
        return hosting_service_error(
            origin,
            "interpreted native Write requires an opaque native object"
        );
    }
    return hosting_invoke(
        values,
        call,
        contract,
        context,
        host,
        0uz,
        request.parameters,
        request.result,
        request.failures,
        arguments,
        limits,
        contract.native_types,
        origin,
        execution_context,
        environment_effects
    );
}
