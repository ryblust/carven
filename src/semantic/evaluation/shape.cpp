module carven:semantic.evaluation.shape.impl;

import :semantic.evaluation.limits;
import :semantic.evaluation.shape;
import std;

ExecutionTypeShapes::ExecutionTypeShapes(const ExecutionValueAccess& values) noexcept
    : values(values) {}

auto ExecutionTypeShapes::get(ConstructionTypeRef type) const noexcept
    -> std::optional<ExecutionTypeShape> {
    return compute(type, 0uz);
}

auto ExecutionTypeShapes::compute(ConstructionTypeRef type, std::size_t depth) const noexcept
    -> std::optional<ExecutionTypeShape> {
    type = execution_type_reference(values, type);
    if (const auto found = completed.find(type); found != completed.end()) {
        return depth <= maximum_constant_aggregate_depth
                && found->second.depth <= maximum_constant_aggregate_depth - depth
            ? std::optional(found->second)
            : std::nullopt;
    }
    if (depth > maximum_constant_aggregate_depth) {
        return std::nullopt;
    }
    constexpr auto saturated = maximum_constant_aggregate_elements + 1uz;
    const auto fields_shape =
        [&](const auto& fields) noexcept -> std::optional<ExecutionTypeShape> {
        auto result = ExecutionTypeShape {
            .exact = true,
            .depth = 1uz,
            .elements = std::min(fields.size(), saturated)
        };
        for (const auto field : fields) {
            const auto child = compute(field, depth + 1uz);
            if (!child) {
                return std::nullopt;
            }
            result.depth = std::max(result.depth, child->depth + 1uz);
            result.exact &= child->exact;
            result.elements += std::min(child->elements, saturated - result.elements);
        }
        return result;
    };
    const auto result = visit_execution_type(
        values,
        type,
        [&](const auto& shape) noexcept -> std::optional<ExecutionTypeShape> {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, VectorTypeValue>
                          || std::same_as<Shape, GenericVectorType>) {
                return ExecutionTypeShape {.exact = false, .depth = 0uz, .elements = 0uz};
            } else if constexpr (std::same_as<Shape, ArrayTypeValue>
                                 || std::same_as<Shape, ConstructionArrayTypeValue>
                                 || std::same_as<Shape, GenericArrayType>) {
                if (shape.extent == 0u) {
                    return ExecutionTypeShape {.exact = true, .depth = 1uz, .elements = 0uz};
                }
                const auto child = compute(shape.element, depth + 1uz);
                if (!child) {
                    return std::nullopt;
                }
                const auto count = 1uz + child->elements;
                return ExecutionTypeShape {
                    .exact = child->exact,
                    .depth = child->depth + 1uz,
                    .elements = shape.extent > maximum_constant_aggregate_elements / count
                        ? saturated
                        : static_cast<std::size_t>(shape.extent) * count
                };
            } else if constexpr (std::same_as<Shape, TupleTypeValue>
                                 || std::same_as<Shape, ConstructionTupleTypeValue>
                                 || std::same_as<Shape, GenericTupleType>) {
                return fields_shape(shape.elements);
            } else if constexpr (std::same_as<Shape, StructTypeValue>) {
                const auto fields = values.struct_field_types(shape.structure);
                return fields ? fields_shape(*fields) : std::nullopt;
            } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
                const auto cases = values.enum_case_types(shape.enumeration);
                if (!cases) {
                    return std::nullopt;
                }
                auto result = ExecutionTypeShape {.exact = true, .depth = 1uz, .elements = 0uz};
                for (auto index = 0uz; index < cases->size(); ++index) {
                    const auto payload = fields_shape((*cases)[index].payload_types);
                    if (!payload) {
                        return std::nullopt;
                    }
                    result.exact &= payload->exact
                        && (index == 0uz
                            || (result.depth == payload->depth
                                && result.elements == payload->elements));
                    result.depth = std::max(result.depth, payload->depth);
                    result.elements = std::max(result.elements, payload->elements);
                }
                return result;
            } else if constexpr (std::same_as<Shape, BuiltinTypeValue>
                                 || std::same_as<Shape, RangeTypeValue>
                                 || std::same_as<Shape, PointerTypeValue>
                                 || std::same_as<Shape, GenericPointerType>
                                 || std::same_as<Shape, SliceTypeValue>
                                 || std::same_as<Shape, ConstructionSliceTypeValue>
                                 || std::same_as<Shape, GenericSliceType>
                                 || std::same_as<Shape, FunctionTypeValue>
                                 || std::same_as<Shape, ClosureTypeValue>
                                 || std::same_as<Shape, CallableViewTypeValue>
                                 || std::same_as<Shape, ConstructionCallableViewTypeValue>
                                 || std::same_as<Shape, GenericCallableViewType>
                                 || std::same_as<Shape, OperationTypeValue>
                                 || std::same_as<Shape, ConstructionOperationTypeValue>
                                 || std::same_as<Shape, GenericOperationType>
                                 || std::same_as<Shape, CppTypeValue>) {
                return ExecutionTypeShape {.exact = true, .depth = 0uz, .elements = 0uz};
            } else {
                return std::nullopt;
            }
        }
    );
    if (result && result->depth > maximum_constant_aggregate_depth - depth) {
        return std::nullopt;
    }
    if (result) {
        completed.emplace(type, *result);
    }
    return result;
}
