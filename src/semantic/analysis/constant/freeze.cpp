module carven:semantic.analysis.constant.freeze.impl;

import :semantic.analysis.constant.freeze;
import :semantic.evaluation.limits;
import :semantic.evaluation.shape;
import std;

namespace {

auto freeze_value(
    ProgramDraft& draft,
    const ExecutionTypeShapes& shapes,
    ExecutionValue value,
    std::size_t depth
) noexcept -> std::optional<ConstantID> {
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        static_cast<void>(draft.constant(*constant));
        return *constant;
    }
    if (const auto* atom = std::get_if<ConstantAtom>(&value)) {
        return draft.intern_constant(constant_fact(*atom));
    }
    if (const auto text = execution_text(draft, value)) {
        return draft.intern_constant({
            .type = draft.builtin_type(BuiltinType::Str),
            .value = StringConstant {.value = draft.intern_spelling(*text)},
        });
    }
    if (std::holds_alternative<ExecutionPointer>(value)) {
        if (const auto atom = execution_atom(draft, value)) {
            return draft.intern_constant(constant_fact(*atom));
        }
        return std::nullopt;
    }
    const auto* aggregate = std::get_if<ExecutionAggregateValue>(&value);
    const auto* enumeration = std::get_if<ExecutionEnumValue>(&value);
    if (aggregate || enumeration) {
        const auto reference = aggregate ? aggregate->type : ConstructionTypeRef(enumeration->type);
        const auto* concrete = std::get_if<TypeID>(&reference);
        if (!concrete) {
            return std::nullopt;
        }
        const auto type = *concrete;
        const auto enum_case = enumeration ? std::optional(enumeration->enum_case) : std::nullopt;
        const auto children = execution_elements(value);
        if (depth >= maximum_constant_aggregate_depth
            || children.size() > maximum_constant_aggregate_elements) {
            return std::nullopt;
        }
        const auto canonical = draft.type_copy(type);
        if (enum_case.has_value() != std::holds_alternative<EnumTypeValue>(canonical.value)) {
            return std::nullopt;
        }
        const auto* array = std::get_if<ArrayTypeValue>(&canonical.value);
        const auto* slice = std::get_if<SliceTypeValue>(&canonical.value);
        const auto* structure = std::get_if<StructTypeValue>(&canonical.value);
        const auto fields =
            structure ? draft.struct_field_types(structure->structure) : std::nullopt;
        auto payload = std::optional<std::vector<TypeID>>();
        if (const auto* enumeration_type = std::get_if<EnumTypeValue>(&canonical.value)) {
            const auto cases = draft.enum_case_types(enumeration_type->enumeration);
            if (!cases) {
                return std::nullopt;
            }
            for (const auto& item : *cases) {
                if (item.id == enum_case) {
                    payload = item.payload_types;
                    break;
                }
            }
            if (!payload || payload->size() != children.size()) {
                return std::nullopt;
            }
        }
        if (!slice) {
            const auto shape = shapes.get(type);
            if (!shape
                || !shape->supported
                || shape->elements > maximum_constant_aggregate_elements
                || (array && children.size() != array->extent)
                || (!array && !enum_case && (!fields || fields->size() != children.size()))) {
                return std::nullopt;
            }
        }
        auto elements = std::vector<ConstantID>();
        elements.reserve(children.size());
        for (auto index = 0uz; index < children.size(); ++index) {
            auto& child = children[index];
            const auto reference = execution_value_type(draft, child);
            const auto* actual = std::get_if<TypeID>(&reference);
            if (!actual) {
                return std::nullopt;
            }
            const auto expected = array ? array->element
                : slice                 ? slice->element
                : fields                ? (*fields)[index]
                : payload               ? (*payload)[index]
                                        : *actual;
            if (*actual != expected) {
                return std::nullopt;
            }
            const auto frozen = freeze_value(draft, shapes, std::move(child), depth + 1uz);
            if (!frozen || draft.constant(*frozen).type != expected) {
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
        return draft.intern_constant({.type = type, .value = std::move(frozen)});
    }
    return std::nullopt;
}

} // namespace

auto freeze_constant_value(ProgramDraft& draft, ExecutionValue value) noexcept
    -> std::optional<ConstantID> {
    const auto shapes = ExecutionTypeShapes(draft);
    return freeze_value(draft, shapes, std::move(value), 0);
}

auto constant_initializer_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> ConstructionTypeRef {
    const auto* id = std::get_if<TypeID>(&type);
    if (id
        && draft.type_copy(*id).value
            == CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::String}}) {
        return draft.builtin_type(BuiltinType::Str);
    }
    return type;
}
