module;

#include <carven/hosting/operation.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.operation;

import :hosting.value;
import :semantic.evaluation.execution;
import :semantic.evaluation.native.request;
import :semantic.evaluation.value;
import :semantic.semir.values;
import std;

auto hosting_cpp_kind(ExecutionNativeKind kind) noexcept -> HostCppOperationKind;

auto hosting_cpp_matches(
    const HostCallContract& contract,
    const ExecutionValueAccess& values,
    const ExecutionNativeRequest& request,
    std::span<const ExecutionValue> arguments
) noexcept -> bool;

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
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;

template<typename Bindings>
    requires std::ranges::input_range<const Bindings>
    && std::is_lvalue_reference_v<std::ranges::range_reference_t<const Bindings>>
auto hosting_cpp_select(
    const ExecutionValueAccess& values,
    const Bindings& bindings,
    const ExecutionNativeRequest& request,
    std::span<const ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<const std::ranges::range_value_t<Bindings>*, ExecutionCallFailure> {
    const auto* selected = static_cast<const std::ranges::range_value_t<Bindings>*>(nullptr);
    auto named = false;
    for (const auto& binding : bindings) {
        named = named
            || (binding.contract.cpp_operation == hosting_cpp_kind(request.kind)
                && binding.contract.name == request.name);
        if (hosting_cpp_matches(binding.contract, values, request, arguments)) {
            if (selected != nullptr) {
                return hosting_service_error(origin, "native C++ operation has ambiguous bindings");
            }
            selected = &binding;
        }
    }
    if (selected != nullptr) {
        return selected;
    }
    return hosting_service_error(
        origin,
        named ? "native C++ operation does not match its registered contract"
              : "native C++ operation has no registered implementation"
    );
}
