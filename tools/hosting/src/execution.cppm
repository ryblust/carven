module;

#include <carven/hosting/api.hpp>

module carven:hosting.execution;

import :hosting.environment;
import :hosting.operation;
import :semantic.analysis.program;
import :semantic.evaluation.execution;
import :semantic.evaluation.native.request;
import :semantic.evaluation.value;
import std;

auto hosting_native_call(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const HostEnvironment& environment,
    HostCallContext& host,
    const ExecutionNativeRequest& request,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;

auto hosting_analysis_call(
    ProgramDraft& draft,
    SemanticExecutionContext& context,
    const HostEnvironment& environment,
    HostCallContext& host,
    ExecutionCallSite target,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;

auto hosting_execution_call(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const HostEnvironment& environment,
    HostCallContext& host,
    ExecutionCallSite target,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;
