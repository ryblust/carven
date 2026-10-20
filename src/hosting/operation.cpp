module;

#include <carven/hosting/operation.hpp>

#include <carven/hosting/value.hpp>

module carven:hosting.operation.impl;

import :hosting.operation;
import :hosting.value;
import :semantic.evaluation.value;
import :semantic.semir.structured;
import std;

auto host_cpp_matches(
    const HostCallContract& contract,
    const SemIRProgram& program,
    const SemanticExpression& expression,
    std::span<const ExecutionValue> arguments
) noexcept -> bool {
    if (!contract.cpp_operation || contract.parameters.size() != arguments.size()) {
        return false;
    }
    const auto global_name = [](const CppNameReference& reference) static noexcept {
        auto name = std::string();
        if (reference.lookup == CppNameLookup::Global) {
            for (const auto& component : reference.components) {
                if (!name.empty()) {
                    name += "::";
                }
                name += component;
            }
        }
        return name;
    };
    auto kind = HostCppOperationKind::Function;
    auto name = std::string();
    if (const auto* call = std::get_if<SemCppCall>(&expression.value)) {
        if (const auto* function = std::get_if<CppNameReference>(&call->callee)) {
            name = global_name(*function);
        } else if (const auto* method =
                       std::get_if<CppMemberCallee<SemCppOperand>>(&call->callee)) {
            kind = HostCppOperationKind::Method;
            name = method->member;
        } else {
            return false;
        }
    } else if (const auto* operation = std::get_if<SemCpp>(&expression.value)) {
        if (const auto* field = std::get_if<CppMemberOperation>(&operation->operation)) {
            kind = HostCppOperationKind::Field;
            name = field->name;
        } else if (std::holds_alternative<CppConvertOperation>(operation->operation)) {
            kind = HostCppOperationKind::Conversion;
            name = "conversion";
            if (const auto* native = std::get_if<CppTypeValue>(
                    &program.types().type(expression.type.resolved()).value
                )) {
                const auto* target = std::get_if<CppNamedType>(&native->form);
                if (target == nullptr || !target->arguments.empty()) {
                    return false;
                }
                name = global_name(target->name);
            }
        } else {
            return false;
        }
    } else {
        return false;
    }
    if (*contract.cpp_operation != kind || contract.name != name) {
        return false;
    }
    auto matches = true;
    auto index = 0uz;
    const auto operand = [&](AccessMode access, const SemanticExpression& input) noexcept {
        if (index >= arguments.size()) {
            matches = false;
            return;
        }
        const auto& parameter = contract.parameters[index];
        if ((parameter.access == HostCallAccess::Write && access == AccessMode::Read)
            || (parameter.access == HostCallAccess::Take && access != AccessMode::Take)) {
            matches = false;
        }
        if (const auto* native = std::get_if<ExecutionHostObject>(&arguments[index])) {
            matches = matches
                && carven::runtime::hosting::detail::ObjectAccess::resolve(
                       native->reference,
                       parameter.type.describe().native_type
                   ) != nullptr;
        } else {
            matches = matches
                && hosting_call_type(
                          program,
                          input.type.resolved(),
                          parameter.type,
                          contract.native_types
                );
        }
        ++index;
    };
    expression.value.visit([&](const auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemCpp> || std::same_as<Operation, SemCppCall>) {
            visit_cpp_operands(operation, operand);
        }
    });
    auto result = expression.type.resolved();
    if (contract.execution == HostCallExecution::Async) {
        const auto* operation =
            std::get_if<OperationTypeValue>(&program.types().type(result).value);
        if (operation == nullptr
            || !hosting_failures_match(
                program,
                operation->failures,
                contract.failures,
                contract.native_types
            )) {
            return false;
        }
        result = operation->success;
    }
    return matches
        && index == arguments.size()
        && hosting_call_type(program, result, contract.result, contract.native_types);
}

auto host_cpp_call(
    const SemIRProgram& program,
    HostTypedCall call,
    const HostCallContract& contract,
    const SemanticExpression& expression,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    auto parameters = std::vector<CallableParameter>();
    expression.value.visit([&](const auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemCpp> || std::same_as<Operation, SemCppCall>) {
            visit_cpp_operands(
                operation,
                [&](AccessMode access, const SemanticExpression& input) noexcept {
                    parameters.push_back(
                        {.stage = ParameterStage::Runtime,
                         .access = access,
                         .type = input.type.resolved()}
                    );
                }
            );
        }
    });
    if (std::ranges::any_of(parameters, [&](const auto& parameter) noexcept {
            return parameter.access == AccessMode::Write
                && !std::holds_alternative<CppTypeValue>(
                       program.types().type(parameter.type).value
                );
        })) {
        return std::unexpected(
            ExecutionEvent {
                .origin = origin,
                .cause =
                    ExecutionIssue {
                        .reason = ExecutionReason::Admission,
                        .message = "interpreted native Write requires an opaque native object",
                        .termination = ExecutionTermination::StopRoot
                    },
                .fields = {},
                .calls = {},
                .blocks = {}
            }
        );
    }
    auto result = expression.type.resolved();
    auto failures = std::span<const TypeID>();
    if (const auto* operation =
            std::get_if<OperationTypeValue>(&program.types().type(result).value)) {
        result = operation->success;
        failures = program.failure_sets().failure_set(operation->failures).members;
    }
    return hosting_invoke(
        program,
        call,
        contract.result,
        nullptr,
        nullptr,
        0uz,
        parameters,
        result,
        failures,
        arguments,
        limits,
        contract.native_types,
        origin
    );
}

auto host_cpp_call(
    const SemIRProgram& program,
    std::span<const HostServiceBinding> services,
    const SemanticExpression& expression,
    std::span<const ExecutionValue> arguments,
    HostLimits limits,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    return host_cpp_call(
        program,
        services | std::views::transform(HostServiceAccess::registration),
        expression,
        arguments,
        limits,
        origin
    );
}
