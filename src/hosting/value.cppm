module;

#include <carven/hosting/value.hpp>

module carven:hosting.value;

import :semantic.evaluation.execution;
import :semantic.evaluation.limits;
import :semantic.evaluation.value;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import std;

auto hosting_type_supported(const SemIRProgram& program, TypeID type, bool allow_void) noexcept
    -> bool;

auto hosting_signature_supported(
    const SemIRProgram& program,
    const CallableSignature& signature,
    bool allow_failures,
    bool allow_async
) noexcept -> bool;

auto hosting_import(
    const SemIRProgram& program,
    const HostValue& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types = {}
) noexcept -> std::expected<ExecutionValue, HostError>;

auto hosting_export(
    const SemIRProgram& program,
    const ExecutionValue& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types
) noexcept -> std::expected<HostValue, HostError>;

auto hosting_native_type_name(const SemIRProgram& program, TypeID type) noexcept
    -> std::optional<std::string>;

auto hosting_call_type(
    const SemIRProgram& program,
    TypeID type,
    const HostCallType& entry,
    std::span<const HostNativeType> native_types
) noexcept -> bool;

auto hosting_failure_type(const SemIRProgram& program, TypeID type) noexcept
    -> std::optional<HostFailureType>;

auto hosting_failures_match(
    const SemIRProgram& program,
    FailureSetID failures,
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

auto hosting_invoke(
    const SemIRProgram& program,
    HostTypedCall call,
    const HostCallType& result_contract,
    void* context,
    HostCallContext* host,
    std::size_t index,
    std::span<const CallableParameter> parameters,
    TypeID result,
    std::span<const TypeID> failures,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    std::span<const HostNativeType> native_types,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;
