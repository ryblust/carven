module;

#include <carven/hosting/operation.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.operation;

import :hosting.value;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

auto host_cpp_matches(
    const HostCallContract& contract,
    const SemIRProgram& program,
    const SemanticExpression& expression,
    std::span<const ExecutionValue> arguments
) noexcept -> bool;

auto host_cpp_call(
    const SemIRProgram& program,
    HostTypedCall call,
    const HostCallContract& contract,
    const SemanticExpression& expression,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;

template<typename Bindings>
    requires std::ranges::input_range<const Bindings>
    && std::is_lvalue_reference_v<std::ranges::range_reference_t<const Bindings>>
auto host_cpp_call(
    const SemIRProgram& program,
    const Bindings& bindings,
    const SemanticExpression& expression,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    auto selected = HostTypedCall();
    const auto* contract = static_cast<const HostCallContract*>(nullptr);
    for (const auto& binding : bindings) {
        if (host_cpp_matches(binding.contract, program, expression, arguments)) {
            if (selected != nullptr) {
                return hosting_service_error(origin, "native C++ operation has ambiguous bindings");
            }
            selected = binding.call;
            contract = std::addressof(binding.contract);
        }
    }
    if (selected != nullptr) {
        return host_cpp_call(program, selected, *contract, expression, arguments, limits, origin);
    }
    return std::unexpected(
        ExecutionEvent {
            .origin = origin,
            .cause =
                ExecutionIssue {
                    .reason = ExecutionReason::Admission,
                    .message = "native C++ operation has no registered implementation",
                    .termination = ExecutionTermination::StopRoot
                },
            .fields = {},
            .calls = {},
            .blocks = {}
        }
    );
}

auto host_cpp_call(
    const SemIRProgram& program,
    std::span<const HostServiceBinding> services,
    const SemanticExpression& expression,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;
