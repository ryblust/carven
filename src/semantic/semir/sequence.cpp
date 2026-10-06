module carven:semantic.semir.sequence.impl;

import :semantic.semir.sequence;
import std;

auto unsupported_sequence_element(const ExecutionValueAccess& values, TypeID element) noexcept
    -> std::optional<std::string_view> {
    auto pending = std::vector<TypeID> {element};
    auto visited = std::set<TypeID>();
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto type = values.type_copy(current);
        const auto unsupported = type.value.visit([](const auto& shape) static noexcept {
            return unsupported_sequence_element_shape(shape);
        });
        if (unsupported) {
            return unsupported;
        }
        if (const auto* array = std::get_if<ArrayTypeValue>(&type.value)) {
            pending.push_back(array->element);
        } else if (const auto* sequence = std::get_if<OwnedSequenceTypeValue>(&type.value)) {
            pending.push_back(sequence->element);
        } else if (const auto* structure = std::get_if<StructTypeValue>(&type.value)) {
            const auto fields = values.struct_field_types(structure->structure);
            if (!fields) {
                return "Sequence element requires completed declaration fields";
            }
            pending.append_range(*fields);
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&type.value)) {
            const auto cases = values.enum_case_types(enumeration->enumeration);
            if (!cases) {
                return "Sequence element requires completed enum payloads";
            }
            for (const auto& member : *cases) {
                pending.append_range(member.payload_types);
            }
        }
        // Raw pointers are values; their targets are not contained storage.
    }
    return std::nullopt;
}
