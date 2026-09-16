module carven:semantic.evaluation.freeze.impl;

import :semantic.evaluation.freeze;
import :semantic.evaluation.limits;
import :semantic.evaluation.shape;
import std;

namespace {

auto freeze_value(
    ConstantValueAccess& values,
    const ExecutionTypeShapes& shapes,
    ExecutionValue value,
    std::size_t depth
) noexcept -> std::optional<ConstantID> {
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        return *constant;
    }
    if (const auto* atom = std::get_if<ConstantAtom>(&value)) {
        return values.intern_constant(constant_fact(*atom));
    }
    if (const auto text = execution_text(values, value)) {
        return values.intern_constant({
            .type = values.builtin_type(BuiltinType::Str),
            .value = StringConstant {.value = values.intern_spelling(*text)},
        });
    }
    const auto* aggregate = std::get_if<ExecutionAggregateValue>(&value);
    const auto* enumeration = std::get_if<ExecutionEnumValue>(&value);
    if (aggregate || enumeration) {
        const auto type = aggregate ? aggregate->type : enumeration->type;
        const auto enum_case = enumeration ? std::optional(enumeration->enum_case) : std::nullopt;
        const auto children = execution_elements(value);
        if (depth >= maximum_constant_aggregate_depth
            || children.size() > maximum_constant_aggregate_elements) {
            return std::nullopt;
        }
        const auto canonical = values.type_copy(type);
        if (enum_case.has_value() != std::holds_alternative<EnumTypeValue>(canonical.value)) {
            return std::nullopt;
        }
        const auto* array = std::get_if<ArrayTypeValue>(&canonical.value);
        const auto* slice = std::get_if<SliceTypeValue>(&canonical.value);
        const auto* structure = std::get_if<StructTypeValue>(&canonical.value);
        const auto fields =
            structure ? values.struct_field_types(structure->structure) : std::nullopt;
        if (!enum_case && !slice) {
            const auto shape = shapes.get(type);
            if (!shape
                || !shape->supported
                || shape->elements > maximum_constant_aggregate_elements
                || (array && children.size() != array->extent)
                || (!array && (!fields || fields->size() != children.size()))) {
                return std::nullopt;
            }
        }
        auto elements = std::vector<ConstantID>();
        elements.reserve(children.size());
        for (auto index = 0uz; index < children.size(); ++index) {
            auto& child = children[index];
            const auto actual = execution_value_type(values, child);
            const auto expected = array ? array->element
                : slice                 ? slice->element
                : fields                ? (*fields)[index]
                                        : actual;
            if (actual != expected) {
                return std::nullopt;
            }
            const auto frozen = freeze_value(values, shapes, std::move(child), depth + 1uz);
            if (!frozen || values.constant(*frozen).type != expected) {
                return std::nullopt;
            }
            elements.push_back(*frozen);
        }
        auto frozen = [&]() noexcept -> ConstantValue {
            if (enum_case) {
                return PayloadEnumConstant {
                    .enum_case = *enum_case,
                    .payload = std::move(elements)
                };
            }
            if (array) {
                return ArrayConstant {.elements = std::move(elements)};
            }
            if (slice) {
                return SliceConstant {.elements = std::move(elements)};
            }
            return StructConstant {.fields = std::move(elements)};
        }();
        return values.intern_constant({.type = type, .value = std::move(frozen)});
    }
    return std::nullopt;
}

} // namespace

auto freeze_constant_value(ConstantValueAccess& values, ExecutionValue value) noexcept
    -> std::optional<ConstantID> {
    const auto shapes = ExecutionTypeShapes(values);
    return freeze_value(values, shapes, std::move(value), 0);
}
