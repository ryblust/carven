module;

#include <carven/hosting/value.hpp>

module carven:hosting.value;

import :semantic.evaluation.execution;
import :semantic.evaluation.limits;
import :semantic.evaluation.value;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.semir.values;
import std;

auto hosting_type_supported(
    const ExecutionValueAccess& values,
    TypeID type,
    bool allow_void
) noexcept -> bool;

auto hosting_signature_supported(
    const SemIRProgram& program,
    const CallableSignature& signature,
    bool allow_failures,
    bool allow_async
) noexcept -> bool;

auto hosting_import(
    const ExecutionValueAccess& values,
    const HostValue& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types = {}
) noexcept -> std::expected<ExecutionValue, HostError>;

auto hosting_import(
    const ExecutionValueAccess& values,
    HostValue&& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types = {}
) noexcept -> std::expected<ExecutionValue, HostError>;

auto hosting_export(
    const ExecutionValueAccess& values,
    const ExecutionValue& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types
) noexcept -> std::expected<HostValue, HostError>;

auto hosting_call_type(
    const ExecutionValueAccess& values,
    TypeID type,
    const HostCallType& entry,
    std::span<const HostNativeType> native_types
) noexcept -> bool;

auto hosting_failure_type(const ExecutionValueAccess& values, TypeID type) noexcept
    -> std::optional<HostFailureType>;

auto hosting_failures_match(
    const ExecutionValueAccess& values,
    std::span<const TypeID> failures,
    std::span<const HostFailureType> contract,
    std::span<const HostNativeType> native_types
) noexcept -> bool;

auto hosting_service_error(ProgramOriginID origin, std::string message) noexcept
    -> std::unexpected<ExecutionCallFailure>;

auto hosting_contract_matches(
    const SemIRProgram& program,
    CallableSignatureID signature,
    const HostCallContract& contract
) noexcept -> bool;

auto hosting_contract_matches(
    const ExecutionValueAccess& values,
    const CallableSignature& signature,
    const HostCallContract& contract
) noexcept -> bool;

auto hosting_invoke(
    const ExecutionValueAccess& values,
    HostTypedCall call,
    const HostCallContract& contract,
    void* context,
    HostCallContext* host,
    std::size_t index,
    std::span<const CallableParameter> parameters,
    TypeID result,
    std::span<const TypeID> failures,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    std::span<const HostNativeType> native_types,
    ProgramOriginID origin,
    SemanticExecutionContext& execution_context,
    bool environment_effects
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;
