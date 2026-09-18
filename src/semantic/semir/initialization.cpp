module carven:semantic.semir.initialization.impl;

import :semantic.semir.initialization;
import :support.invariant;
import std;

auto default_initialization(const ExecutionValueAccess& types, TypeID type) noexcept
    -> DefaultInitialization {
    return query_default_initialization(
        type,
        [&](ConstructionTypeRef reference) noexcept -> InitializationType {
            const auto* concrete = std::get_if<TypeID>(&reference);
            if (concrete == nullptr) {
                invariant_violation("published default initialization has an unresolved type");
            }
            return types.type_copy(*concrete);
        },
        [&](StructID structure) noexcept {
            const auto fields = types.struct_field_types(structure);
            if (!fields) {
                invariant_violation("default initialization requires completed field types");
            }
            return std::vector<ConstructionTypeRef>(fields->begin(), fields->end());
        }
    );
}
