module carven:semantic.analysis.program.capabilities.impl;

import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.type;
import :support.visit;
import std;

namespace {

struct EqualityCapabilities final {
    std::vector<bool> roots;
    std::vector<std::pair<EnumID, bool>> enumerations;
};

auto compute_equality_capabilities(
    const ProgramDraft& draft,
    std::span<const ConstructionTypeRef> roots
) noexcept -> EqualityCapabilities {
    auto indices = std::map<ConstructionTypeRef, std::size_t>();
    auto types = std::vector<ConstructionTypeRef>();
    auto dependents = std::vector<std::vector<std::size_t>>();
    auto supported = std::vector<bool>();
    auto enumerations = std::vector<std::pair<EnumID, std::size_t>>();
    const auto intern = [&](ConstructionTypeRef type) noexcept {
        const auto [entry, inserted] = indices.try_emplace(type, types.size());
        if (inserted) {
            types.push_back(type);
            dependents.emplace_back();
            supported.push_back(true);
        }
        return entry->second;
    };
    auto root_indices = std::vector<std::size_t>();
    for (const auto type : roots) {
        root_indices.push_back(intern(type));
    }
    for (auto index = 0uz; index < types.size(); ++index) {
        const auto type = types[index];
        const auto depend = [&](ConstructionTypeRef child) noexcept {
            const auto child_index = intern(child);
            dependents[child_index].push_back(index);
        };
        if (const auto* term = std::get_if<TypeTermID>(&type)) {
            const auto construction = draft.construction_type_copy(*term);
            if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
                depend(array->element);
            } else {
                supported[index] = false;
            }
            continue;
        }
        draft.type_copy(std::get<TypeID>(type))
            .value.visit(
                Overloaded {
                    [&](const BuiltinTypeValue& value) noexcept {
                        supported[index] = builtin_type_supports_equality(value.kind);
                    },
                    [&](const StructTypeValue&) noexcept { supported[index] = false; },
                    [&](const EnumTypeValue& value) noexcept {
                        enumerations.emplace_back(value.enumeration, index);
                        const auto declaration = draft.enum_declaration_copy(value.enumeration);
                        if (std::holds_alternative<NumericEnumRepresentation>(
                                declaration.representation
                            )) {
                            return;
                        }
                        for (const auto id : declaration.cases) {
                            for (const auto payload :
                                 draft.construction_enum_case_declaration_copy(id).payload_types) {
                                depend(payload);
                            }
                        }
                    },
                    [&](const ArrayTypeValue& array) noexcept { depend(array.element); },
                    [](const PointerTypeValue&) static noexcept {},
                    [&]<typename Value>(const Value&) noexcept {
                        static_assert(
                            std::same_as<Value, FunctionTypeValue>
                            || std::same_as<Value, ClosureTypeValue>
                            || std::same_as<Value, CallableViewTypeValue>
                            || std::same_as<Value, CppTypeValue>
                            || std::same_as<Value, RangeTypeValue>
                            || std::same_as<Value, SliceTypeValue>
                        );
                        supported[index] = false;
                    },
                }
            );
    }
    auto pending = std::vector<std::size_t>();
    for (auto index = 0uz; index < supported.size(); ++index) {
        if (!supported[index]) {
            pending.push_back(index);
        }
    }
    for (auto index = 0uz; index < pending.size(); ++index) {
        for (const auto dependent : dependents[pending[index]]) {
            if (supported[dependent]) {
                supported[dependent] = false;
                pending.push_back(dependent);
            }
        }
    }
    auto result = EqualityCapabilities {};
    for (const auto index : root_indices) {
        result.roots.push_back(supported[index]);
    }
    for (const auto& [enumeration, index] : enumerations) {
        result.enumerations.emplace_back(enumeration, supported[index]);
    }
    return result;
}

} // namespace

auto ProgramDraft::equality_support(std::span<const ConstructionTypeRef> roots) const noexcept
    -> std::vector<bool> {
    return compute_equality_capabilities(*this, roots).roots;
}

auto ProgramDraft::resolve_enum_equality(std::span<const TypeID> roots) noexcept -> void {
    auto types = std::vector<ConstructionTypeRef>();
    for (const auto type : roots) {
        types.push_back(type);
    }
    const auto capabilities = compute_equality_capabilities(*this, types);
    for (const auto& [enumeration, supported] : capabilities.enumerations) {
        storage.declarations.set_enum_equality(enumeration, supported);
    }
}
