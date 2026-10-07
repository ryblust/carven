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
        auto result =
            ExecutionTypeShape {.supported = false, .exact = true, .depth = 0uz, .elements = 0uz};
        constexpr auto saturated = maximum_constant_aggregate_elements + 1uz;
        if (std::holds_alternative<OwnedSequenceTypeValue>(canonical.value)) {
            result.exact = false;
            return result;
        } else if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
            const auto child = compute(array->element, depth + 1uz);
            if (!child) {
                return std::nullopt;
            }
            result.depth = child->depth + 1uz;
            result.supported = child->supported;
            result.exact = child->exact;
            const auto count = 1uz + child->elements;
            result.elements = array->extent > maximum_constant_aggregate_elements / count
                ? saturated
                : static_cast<std::size_t>(array->extent) * count;
        } else if (std::holds_alternative<StructTypeValue>(canonical.value)
                   || std::holds_alternative<TupleTypeValue>(canonical.value)) {
            const auto* tuple = std::get_if<TupleTypeValue>(&canonical.value);
            const auto fields = tuple
                ? std::optional(tuple->elements)
                : values.struct_field_types(std::get<StructTypeValue>(canonical.value).structure);
            if (!fields) {
                return std::nullopt;
            }
            result = {
                .supported = true,
                .exact = true,
                .depth = 1uz,
                .elements = std::min(fields->size(), saturated)
            };
            for (const auto field : *fields) {
                const auto child = compute(field, depth + 1uz);
                if (!child) {
                    return std::nullopt;
                }
                result.depth = std::max(result.depth, child->depth + 1uz);
                result.supported &= child->supported;
                result.exact &= child->exact;
                result.elements += std::min(child->elements, saturated - result.elements);
            }
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value)) {
            const auto cases = values.enum_case_types(enumeration->enumeration);
            if (!cases) {
                return std::nullopt;
            }
            result = {.supported = true, .exact = false, .depth = 1uz, .elements = 0uz};
            for (const auto& item : *cases) {
                auto count = std::min(item.payload_types.size(), saturated);
                for (const auto field : item.payload_types) {
                    const auto child = compute(field, depth + 1uz);
                    if (!child) {
                        return std::nullopt;
                    }
                    result.depth = std::max(result.depth, child->depth + 1uz);
                    result.supported &= child->supported;
                    count += std::min(child->elements, saturated - count);
                }
                result.elements = std::max(result.elements, count);
            }
        } else if (std::holds_alternative<RangeTypeValue>(canonical.value)) {
            result.supported = true;
        } else if (std::holds_alternative<PointerTypeValue>(canonical.value)
                   || std::holds_alternative<SliceTypeValue>(canonical.value)) {
            // Pointers and slices are views. Their backing storage is not contained
            // in the value's aggregate shape.
            result.supported = true;
        } else if (const auto* cpp = std::get_if<CppTypeValue>(&canonical.value)) {
            result.supported = std::holds_alternative<CppConstCharPointerType>(cpp->form);
        } else if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
            result.supported = builtin_is_numeric(builtin->kind)
                || builtin->kind == BuiltinType::U8x32
                || builtin->kind == BuiltinType::Mask32
                || builtin->kind == BuiltinType::F32x8
                || builtin->kind == BuiltinType::Mask8
                || builtin->kind == BuiltinType::U8x16
                || builtin->kind == BuiltinType::Mask16
                || builtin->kind == BuiltinType::F32x4
                || builtin->kind == BuiltinType::Mask4
                || builtin->kind == BuiltinType::Bool
                || builtin->kind == BuiltinType::Char
                || builtin->kind == BuiltinType::Str
                || builtin->kind == BuiltinType::String
                || builtin->kind == BuiltinType::Void;
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
