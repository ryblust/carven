module carven:semantic.evaluation.native.impl;

import :semantic.evaluation.execution;
import :semantic.evaluation.limits;
import :semantic.evaluation.native;
import :semantic.evaluation.native.call;
import :semantic.evaluation.native.request;
import :semantic.evaluation.native.value;
import :semantic.evaluation.runtime;
import :semantic.semir.values;
import :support.utf8;
import std;

ExecutionNativeHost::ExecutionNativeHost(
    std::span<const ExecutionNativeBinding> bindings,
    ExecutionLimits limits
) noexcept
    : bindings(bindings),
      state {.driver = {}, .limits = limits, .stopped = std::nullopt},
      scope(
          {},
          &state,
          [](void* context) static noexcept {
              return !static_cast<ExecutionNativeContext*>(context)->stopped;
          },
          [](void* context, std::string_view message) static noexcept {
              auto& state = *static_cast<ExecutionNativeContext*>(context);
              if (!state.stopped) {
                  state.stopped.emplace(message);
              }
          },
          limits.byte_work,
          maximum_constant_bytes,
          std::min(limits.aggregate_work, maximum_constant_aggregate_elements),
          maximum_constant_aggregate_depth,
          UTF8Decoder::is_valid
      ) {}

auto ExecutionNativeHost::call(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const ExecutionNativeRequest& request,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    if (state.stopped) {
        return execution_native_error(origin, *state.stopped);
    }
    const auto* selected = static_cast<const ExecutionNativeBinding*>(nullptr);
    auto named = false;
    for (const auto& binding : bindings) {
        if (binding.kind != request.kind || binding.name != request.name) {
            continue;
        }
        named = true;
        if (binding.matches(values, request, arguments)) {
            if (selected != nullptr) {
                return execution_native_error(
                    origin,
                    "native C++ operation has ambiguous bindings"
                );
            }
            selected = &binding;
        }
    }
    if (selected == nullptr) {
        return execution_native_error(
            origin,
            named ? "native C++ operation does not match its registered contract"
                  : "native C++ operation has no registered implementation"
        );
    }
    if (context.stage() == ExecutionStage::Static && !selected->capability.static_execution) {
        return execution_native_error(origin, "native binding is not enabled for static execution");
    }
    auto result = selected->invoke(
        state,
        values,
        context,
        request,
        arguments,
        origin,
        selected->capability.environment_effects,
        selected->events
    );
    if (state.stopped) {
        return execution_native_error(origin, *state.stopped);
    }
    return result;
}

auto ExecutionNativeHost::operator()(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    ExecutionCallSite target,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    const auto* expression = std::get_if<const SemanticExpression*>(&target);
    if (expression == nullptr) {
        return execution_native_error(origin, "native function has no registered implementation");
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
        return execution_native_error(origin, "native request has no closed execution contract");
    }
    return call(values, context, *request, arguments, origin);
}
