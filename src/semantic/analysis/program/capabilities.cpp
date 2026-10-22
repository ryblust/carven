module carven:semantic.analysis.program.capabilities.impl;

import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.type;
import :semantic.semir.generic;
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
        type = unwrap_type_alias(draft, type);
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
        if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
            const auto expression = draft.generic_type_copy(*generic);
            if (const auto* array = std::get_if<GenericArrayType>(&expression)) {
                depend(array->element);
            } else if (const auto* tuple = std::get_if<GenericTupleType>(&expression)) {
                for (const auto element : tuple->elements) {
                    depend(element);
                }
            } else if (const auto* parameter = std::get_if<GenericTypeParameter>(&expression)) {
                const auto contract = draft.generic_parameter_contract_copy(parameter->definition);
                supported[index] = std::ranges::contains(
                    contract.parameter_bounds[parameter->index],
                    BuiltinCapability::Equal
                );
            } else {
                supported[index] = std::holds_alternative<GenericPointerType>(expression);
            }
            continue;
        }
        if (const auto* term = std::get_if<TypeTermID>(&type)) {
            const auto construction = draft.construction_type_copy(*term);
            if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
                depend(array->element);
            } else if (const auto* tuple =
                           std::get_if<ConstructionTupleTypeValue>(&construction.value)) {
                for (const auto element : tuple->elements) {
                    depend(element);
                }
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
                    [&](const TupleTypeValue& tuple) noexcept {
                        for (const auto element : tuple.elements) {
                            depend(element);
                        }
                    },
                    [](const PointerTypeValue&) static noexcept {},
                    [&]<typename Value>(const Value&) noexcept {
                        static_assert(
                            std::same_as<Value, FunctionTypeValue>
                            || std::same_as<Value, ClosureTypeValue>
                            || std::same_as<Value, CallableViewTypeValue>
                            || std::same_as<Value, OperationTypeValue>
                            || std::same_as<Value, CppTypeValue>
                            || std::same_as<Value, RangeTypeValue>
                            || std::same_as<Value, SliceTypeValue>
                            || std::same_as<Value, VectorTypeValue>
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
    auto result = EqualityCapabilities {.roots = {}, .enumerations = {}};
    for (const auto index : root_indices) {
        result.roots.push_back(supported[index]);
    }
    for (const auto& [enumeration, index] : enumerations) {
        result.enumerations.emplace_back(enumeration, supported[index]);
    }
    return result;
}

} // namespace

auto type_satisfies_capability(
    const ProgramDraft& draft,
    ConstructionTypeRef type,
    BuiltinCapability capability
) noexcept -> bool {
    if (capability == BuiltinCapability::Equal) {
        return type_supports_equality(draft, type);
    }
    type = unwrap_type_alias(draft, type);
    const auto child = [&](ConstructionTypeRef element) noexcept {
        return type_satisfies_capability(draft, element, capability);
    };
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto shape = draft.generic_type_copy(*generic);
        if (const auto* parameter = std::get_if<GenericTypeParameter>(&shape)) {
            const auto contract = draft.generic_parameter_contract_copy(parameter->definition);
            return std::ranges::contains(contract.parameter_bounds[parameter->index], capability);
        }
        if (const auto* concrete = std::get_if<TypeID>(&shape)) {
            return child(*concrete);
        }
        if (const auto* array = std::get_if<GenericArrayType>(&shape)) {
            return child(array->element);
        }
        if (const auto* tuple = std::get_if<GenericTupleType>(&shape)) {
            return std::ranges::all_of(tuple->elements, child);
        }
        return false;
    }
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto shape = draft.construction_type_copy(*term);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&shape.value)) {
            return child(array->element);
        }
        if (const auto* tuple = std::get_if<ConstructionTupleTypeValue>(&shape.value)) {
            return std::ranges::all_of(tuple->elements, child);
        }
        return false;
    }
    const auto shape = draft.type_copy(std::get<TypeID>(type));
    return hash_type_support(shape, child);
}

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
