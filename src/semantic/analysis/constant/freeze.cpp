module carven:semantic.analysis.constant.freeze.impl;

import :semantic.analysis.constant.freeze;
import :semantic.evaluation.limits;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.type;
import std;

namespace {

auto freeze_value(
    ProgramDraft& draft,
    ExecutionValue value,
    std::size_t depth,
    std::size_t& slots
) noexcept -> std::optional<ConstantID> {
    if (const auto* bytes = std::get_if<ExecutionBytes>(&value)) {
        const auto* type = std::get_if<TypeID>(&bytes->type);
        if (!type
            || bytes->element != draft.builtin_type(BuiltinType::U8)
            || bytes->bytes.capacity() > maximum_constant_bytes
            || (!bytes->bytes.empty() && depth >= maximum_constant_aggregate_depth)) {
            return std::nullopt;
        }
        const auto canonical = draft.type_copy(*type);
        const auto* vector = std::get_if<VectorTypeValue>(&canonical.value);
        const auto* slice = std::get_if<SliceTypeValue>(&canonical.value);
        if ((!vector && !slice) || (vector ? vector->element : slice->element) != bytes->element) {
            return std::nullopt;
        }
        return draft.intern_constant({
            .type = *type,
            .value = ByteSequenceConstant {
                .bytes = std::vector(bytes->bytes.begin(), bytes->bytes.end()),
                .capacity = vector ? bytes->bytes.capacity() : bytes->bytes.size(),
            },
        });
    }
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        const auto& fact = draft.constant(*constant);
        if (const auto* bytes = std::get_if<ByteSequenceConstant>(&fact.value)) {
            return bytes->capacity <= maximum_constant_bytes
                    && (bytes->bytes.empty() || depth < maximum_constant_aggregate_depth)
                ? std::optional(*constant)
                : std::nullopt;
        }
    }
    if (const auto compound = execution_compound_view(draft, value)) {
        if (depth >= maximum_constant_aggregate_depth
            || compound->size() > maximum_constant_aggregate_elements - slots) {
            return std::nullopt;
        }
        slots += compound->size();
    }
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        const auto& fact = draft.constant(*constant);
        if (const auto children = constant_children(fact.value)) {
            for (const auto child : *children) {
                if (!freeze_value(draft, ExecutionValue(child), depth + 1uz, slots)) {
                    return std::nullopt;
                }
            }
        }
        return *constant;
    }
    if (const auto atom = execution_atom(draft, value)) {
        return draft.intern_constant(constant_fact(*atom));
    }
    const auto* chars = std::get_if<ExecutionChars>(&value);
    if (const auto text =
            chars ? chars->text.observe(draft, nullptr) : execution_text(draft, value)) {
        return draft.intern_constant({
            .type = std::get<TypeID>(execution_value_type(draft, value)),
            .value = TextConstant {.value = draft.intern_spelling(text->view())},
        });
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
        const auto canonical = draft.type_copy(type);
        if (enum_case.has_value() != std::holds_alternative<EnumTypeValue>(canonical.value)) {
            return std::nullopt;
        }
        const auto* array = std::get_if<ArrayTypeValue>(&canonical.value);
        const auto* vector = std::get_if<VectorTypeValue>(&canonical.value);
        const auto* tuple = std::get_if<TupleTypeValue>(&canonical.value);
        const auto unit =
            canonical.value == CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::Void}};
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
        if ((array && children.size() != array->extent)
            || (tuple && children.size() != tuple->elements.size())
            || (unit && !children.empty())
            || (!array
                && !vector
                && !tuple
                && !unit
                && !slice
                && !enum_case
                && (!fields || fields->size() != children.size()))) {
            return std::nullopt;
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
                : vector                ? vector->element
                : tuple                 ? tuple->elements[index]
                : slice                 ? slice->element
                : fields                ? (*fields)[index]
                : payload               ? (*payload)[index]
                                        : *actual;
            if (*actual != expected) {
                return std::nullopt;
            }
            const auto frozen = freeze_value(draft, std::move(child), depth + 1uz, slots);
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
            if (vector) {
                return VectorConstant {
                    .elements = std::move(elements),
                    .capacity = aggregate->capacity
                };
            }
            if (tuple) {
                return TupleConstant {.elements = std::move(elements)};
            }
            if (unit) {
                return VoidConstant {};
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
    auto slots = 0uz;
    return freeze_value(draft, std::move(value), 0uz, slots);
}
