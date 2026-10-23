module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.invoke.impl;

import :hosting.value;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.program;
import std;

namespace {

auto import_outcome(
    const ExecutionValueAccess& program,
    HostOutcome outcome,
    bool read_borrows,
    TypeID result,
    std::span<const TypeID> failures,
    HostLimits limits,
    std::span<const HostNativeType> native_types,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionValue, ExecutionCallFailure> {
    if (auto* value = std::get_if<HostValue>(&outcome)) {
        if (std::holds_alternative<CppTypeValue>(program.type_copy(result).value)) {
            auto* native = std::get_if<HostObjectRef>(value);
            if (native == nullptr) {
                return hosting_service_error(origin, "native C++ result has no carrier");
            }
            return ExecutionHostObject(result, std::move(*native), read_borrows);
        }
        auto converted = hosting_import(program, std::move(*value), result, limits, native_types);
        if (!converted) {
            return hosting_service_error(origin, std::move(converted.error().message));
        }
        return std::move(*converted);
    }
    if (auto* failure = std::get_if<HostFailure>(&outcome)) {
        for (const auto type : failures) {
            const auto declared = hosting_failure_type(program, type);
            if (!declared
                || declared->module_path != failure->module_path()
                || declared->name != failure->name()) {
                continue;
            }
            auto converted =
                hosting_import(program, std::move(*failure).payload(), type, limits, native_types);
            if (!converted) {
                return hosting_service_error(origin, std::move(converted.error().message));
            }
            return std::unexpected<ExecutionCallFailure>(ExecutionFailure(
                ExecutionSourceFailure {
                    .type = type,
                    .payload = std::make_shared<const ExecutionValue>(std::move(*converted)),
                    .origin = origin,
                    .calls = {},
                    .blocks = {}
                }
            ));
        }
        return hosting_service_error(origin, "native call returned an undeclared failure");
    }
    return std::unexpected<ExecutionCallFailure>(
        ExecutionFailure(ExecutionCancelled {.origin = origin, .calls = {}, .blocks = {}})
    );
}

// A native activation keeps its own typed arguments and operation. Only this
// compiler-side boundary knows the source program used to import its completion.
class HostExternalCall final : public ExecutionExternalOperation {
public:
    HostExternalCall(
        std::unique_ptr<HostCall> call,
        const ExecutionValueAccess& program,
        TypeID result,
        bool read_borrows,
        std::span<const TypeID> failures,
        HostLimits limits,
        std::span<const HostNativeType> native_types,
        ProgramOriginID origin,
        SemanticExecutionContext& execution_context,
        bool environment_effects
    ) noexcept;

    auto poll() noexcept
        -> std::optional<std::expected<ExecutionValue, ExecutionCallFailure>> override;

    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override;
    auto cancel() noexcept -> void override;

private:
    std::unique_ptr<HostCall> call;
    const ExecutionValueAccess& program;
    TypeID result;
    bool read_borrows;
    std::vector<TypeID> failures;
    HostLimits limits;
    std::vector<HostNativeType> native_types;
    ProgramOriginID origin;
    SemanticExecutionContext& execution_context;
    bool environment_effects;
    bool started;
};

HostExternalCall::HostExternalCall(
    std::unique_ptr<HostCall> call,
    const ExecutionValueAccess& program,
    TypeID result,
    bool read_borrows,
    std::span<const TypeID> failures,
    HostLimits limits,
    std::span<const HostNativeType> native_types,
    ProgramOriginID origin,
    SemanticExecutionContext& execution_context,
    bool environment_effects
) noexcept
    : call(std::move(call)),
      program(program),
      result(result),
      read_borrows(read_borrows),
      failures(failures.begin(), failures.end()),
      limits(limits),
      native_types(native_types.begin(), native_types.end()),
      origin(origin),
      execution_context(execution_context),
      environment_effects(environment_effects),
      started(false) {}

auto HostExternalCall::poll() noexcept
    -> std::optional<std::expected<ExecutionValue, ExecutionCallFailure>> {
    if (!started) {
        started = true;
        if (environment_effects) {
            if (const auto reason = execution_context.host_effects_rejection()) {
                call->cancel();
                return hosting_service_error(origin, std::string(*reason));
            }
        }
    }
    const auto progress = call->advance();
    if (!carven::runtime::hosting::detail::Scope::current().ready()) {
        call->cancel();
        return hosting_service_error(origin, "native operation stopped");
    }
    if (progress != HostTaskStatus::Finished) {
        return std::nullopt;
    }
    auto outcome = call->take_result();
    if (!outcome) {
        return hosting_service_error(origin, std::move(outcome.error().message));
    }
    return import_outcome(
        program,
        std::move(*outcome),
        read_borrows,
        result,
        failures,
        limits,
        native_types,
        origin
    );
}

auto HostExternalCall::wait(std::optional<std::int64_t> deadline) noexcept -> bool {
    return call->wait(deadline);
}

auto HostExternalCall::cancel() noexcept -> void {
    call->cancel();
}

} // namespace

auto hosting_invoke(
    const ExecutionValueAccess& program,
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
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    if (contract.execution == HostCallExecution::Synchronous && environment_effects) {
        if (const auto reason = execution_context.host_effects_rejection()) {
            return hosting_service_error(origin, std::string(*reason));
        }
    }
    auto values = std::vector<HostValue>();
    auto accesses = std::vector<HostCallAccess>();
    values.reserve(arguments.size());
    accesses.reserve(arguments.size());
    auto budget = limits;
    for (auto argument = 0uz; argument < arguments.size(); ++argument) {
        const auto& parameter = parameters[argument];
        switch (parameter.access) {
            case AccessMode::Read:  accesses.push_back(HostCallAccess::Read); break;
            case AccessMode::Write: accesses.push_back(HostCallAccess::Write); break;
            case AccessMode::Take:  accesses.push_back(HostCallAccess::Take); break;
        }
        if (const auto* native = std::get_if<ExecutionHostObject>(&arguments[argument])) {
            values.emplace_back(native->reference);
        } else {
            auto value =
                hosting_export(program, arguments[argument], parameter.type, budget, native_types);
            if (!value) {
                return hosting_service_error(origin, std::move(value.error().message));
            }
            values.push_back(std::move(*value));
        }
    }
    const auto read_borrows = contract.result.describe().read_borrows;
    const auto opaque_result =
        std::holds_alternative<CppTypeValue>(program.type_copy(result).value);
    auto invoked =
        call(context, host, index, accesses, opaque_result, values, limits, native_types);
    if (!invoked) {
        return hosting_service_error(origin, std::move(invoked.error().message));
    }
    if (auto* outcome = std::get_if<HostOutcome>(&*invoked)) {
        auto converted = import_outcome(
            program,
            std::move(*outcome),
            read_borrows,
            result,
            failures,
            limits,
            native_types,
            origin
        );
        if (!converted) {
            return std::unexpected(std::move(converted.error()));
        }
        return ExecutionHostResult(std::move(*converted));
    }
    return ExecutionHostResult(
        std::make_unique<HostExternalCall>(
            std::move(std::get<std::unique_ptr<HostCall>>(*invoked)),
            program,
            result,
            read_borrows,
            failures,
            limits,
            native_types,
            origin,
            execution_context,
            environment_effects
        )
    );
}
