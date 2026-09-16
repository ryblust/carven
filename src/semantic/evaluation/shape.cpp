module carven:semantic.evaluation.shape.impl;

import :semantic.evaluation.limits;
import :semantic.evaluation.shape;
import std;

ExecutionTypeShapes::ExecutionTypeShapes(const ExecutionValueAccess& values) noexcept
    : values(values) {}

auto ExecutionTypeShapes::get(TypeID type) const noexcept -> std::optional<ExecutionTypeShape> {
    return compute(type, 0uz);
}

auto ExecutionTypeShapes::compute(TypeID type, std::size_t depth) const noexcept
    -> std::optional<ExecutionTypeShape> {
    if (const auto found = completed.find(type); found != completed.end()) {
        return depth <= maximum_constant_aggregate_depth
                && found->second.depth <= maximum_constant_aggregate_depth - depth
            ? std::optional(found->second)
            : std::nullopt;
    }
    if (depth > maximum_constant_aggregate_depth) {
        return std::nullopt;
    }
    const auto calculate = [&]() noexcept -> std::optional<ExecutionTypeShape> {
        const auto canonical = values.type_copy(type);
        auto result = ExecutionTypeShape {.supported = false, .depth = 0uz, .elements = 0uz};
        constexpr auto saturated = maximum_constant_aggregate_elements + 1uz;
        if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
            const auto child = compute(array->element, depth + 1uz);
            if (!child) {
                return std::nullopt;
            }
            result.depth = child->depth + 1uz;
            result.supported = child->supported;
            const auto count = 1uz + child->elements;
            result.elements = array->extent > maximum_constant_aggregate_elements / count
                ? saturated
                : static_cast<std::size_t>(array->extent) * count;
        } else if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
            const auto fields = values.struct_field_types(structure->structure);
            if (!fields) {
                return std::nullopt;
            }
            result =
                {.supported = true, .depth = 1uz, .elements = std::min(fields->size(), saturated)};
            for (const auto field : *fields) {
                const auto child = compute(field, depth + 1uz);
                if (!child) {
                    return std::nullopt;
                }
                result.depth = std::max(result.depth, child->depth + 1uz);
                result.supported &= child->supported;
                result.elements += std::min(child->elements, saturated - result.elements);
            }
        } else if (std::holds_alternative<RangeTypeValue>(canonical.value)) {
            result.supported = true;
        } else if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
            result.supported = builtin_is_integer(builtin->kind)
                || builtin->kind == BuiltinType::Bool
                || builtin->kind == BuiltinType::Char
                || builtin->kind == BuiltinType::Str;
        }
        return result;
    };
    const auto result = calculate();
    if (result && result->depth > maximum_constant_aggregate_depth - depth) {
        return std::nullopt;
    }
    if (result) {
        completed.emplace(type, *result);
    }
    return result;
}
