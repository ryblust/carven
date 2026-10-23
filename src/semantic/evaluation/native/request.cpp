module carven:semantic.evaluation.native.request.impl;

import :semantic.evaluation.native.request;
import :semantic.semir.structured;
import std;

namespace {

auto global_name(const CppNameReference& reference) noexcept -> std::string {
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
}

} // namespace

auto execution_native_request(
    const ExecutionValueAccess& values,
    const SemanticExpression& expression,
    FunctionRef<std::optional<TypeID>(ConstructionTypeRef) noexcept> close_type,
    FunctionRef<std::vector<TypeID>(ConstructionFailureRef) noexcept> close_failures
) noexcept -> std::optional<ExecutionNativeRequest> {
    auto result = close_type(expression.type.construction());
    auto failures = std::vector<TypeID>();
    auto execution = CallableExecutionKind::Synchronous;
    visit_execution_type(values, expression.type.construction(), [&](const auto& shape) noexcept {
        using Shape = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<Shape, OperationTypeValue>
                      || std::same_as<Shape, ConstructionOperationTypeValue>
                      || std::same_as<Shape, GenericOperationType>) {
            result = close_type(shape.success);
            failures = close_failures(shape.failures);
            execution = CallableExecutionKind::Async;
        }
    });
    if (!result) {
        return std::nullopt;
    }
    auto kind = ExecutionNativeKind::Function;
    auto name = std::string();
    if (const auto* call = std::get_if<SemCppCall>(&expression.value)) {
        if (const auto* function = std::get_if<CppNameReference>(&call->callee)) {
            name = global_name(*function);
        } else if (const auto* method =
                       std::get_if<CppMemberCallee<SemCppOperand>>(&call->callee)) {
            kind = ExecutionNativeKind::Method;
            name = method->member;
        } else {
            return std::nullopt;
        }
    } else if (const auto* operation = std::get_if<SemCpp>(&expression.value)) {
        if (const auto* field = std::get_if<CppMemberOperation>(&operation->operation)) {
            kind = ExecutionNativeKind::Field;
            name = field->name;
        } else if (std::holds_alternative<CppConvertOperation>(operation->operation)) {
            kind = ExecutionNativeKind::Conversion;
            name = "conversion";
            const auto type = values.type_copy(*result);
            if (const auto* native = std::get_if<CppTypeValue>(&type.value)) {
                const auto* target = std::get_if<CppNamedType>(&native->form);
                if (target == nullptr || !target->arguments.empty()) {
                    return std::nullopt;
                }
                name = global_name(target->name);
            }
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    auto parameters = std::vector<CallableParameter>();
    auto closed = true;
    expression.value.visit([&](const auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemCpp> || std::same_as<Operation, SemCppCall>) {
            visit_cpp_operands(
                operation,
                [&](AccessMode access, const SemanticExpression& input) noexcept {
                    const auto type = close_type(input.type.construction());
                    if (type) {
                        parameters.push_back(
                            {.stage = ParameterStage::Runtime, .access = access, .type = *type}
                        );
                    } else {
                        closed = false;
                    }
                }
            );
        }
    });
    if (!closed) {
        return std::nullopt;
    }
    return ExecutionNativeRequest {
        .kind = kind,
        .name = std::move(name),
        .parameters = std::move(parameters),
        .result = *result,
        .failures = std::move(failures),
        .execution = execution
    };
}
