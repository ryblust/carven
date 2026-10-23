module carven:semantic.evaluation.native.value.impl;

import :semantic.evaluation.limits;
import :semantic.evaluation.native.value;
import :semantic.semir.type;
import std;

auto execution_native_error(ProgramOriginID origin, std::string message) noexcept
    -> std::unexpected<ExecutionCallFailure> {
    return std::unexpected<ExecutionCallFailure>(ExecutionEvent {
        .origin = origin,
        .cause =
            ExecutionIssue {
                .reason = ExecutionReason::Admission,
                .message = std::move(message),
                .termination = ExecutionTermination::StopRoot
            },
        .fields = {},
        .calls = {},
        .blocks = {}
    });
}

auto ExecutionNativeValue::element_type(const ExecutionValueAccess& values, TypeID type) noexcept
    -> std::optional<TypeID> {
    const auto shape = values.type_copy(type);
    if (const auto* vector = std::get_if<VectorTypeValue>(&shape.value)) {
        return vector->element;
    }
    if (const auto* slice = std::get_if<SliceTypeValue>(&shape.value)) {
        return slice->element;
    }
    return std::nullopt;
}

auto ExecutionNativeValue::charge_bytes(std::size_t size, ExecutionLimits& budget) noexcept
    -> std::expected<void, std::string> {
    if (size > maximum_constant_bytes || size > budget.byte_work) {
        return std::unexpected("native value exceeds its byte limit");
    }
    budget.byte_work -= size;
    return {};
}

auto ExecutionNativeValue::charge_elements(std::size_t size, ExecutionLimits& budget) noexcept
    -> std::expected<void, std::string> {
    if (size > maximum_constant_aggregate_elements || size > budget.aggregate_work) {
        return std::unexpected("native value exceeds its aggregate limit");
    }
    budget.aggregate_work -= size;
    return {};
}
