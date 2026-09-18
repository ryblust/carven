module carven:semantic.semir.initialization.impl;

import :semantic.semir.initialization;
import :support.invariant;
import std;

auto default_initialization(const SemIRProgram& program, TypeID type) noexcept
    -> DefaultInitialization {
    return query_default_initialization(
        type,
        [&](ConstructionTypeRef reference) noexcept -> InitializationType {
            const auto* concrete = std::get_if<TypeID>(&reference);
            if (concrete == nullptr) {
                invariant_violation("published default initialization has an unresolved type");
            }
            return program.types().type(*concrete);
        },
        [&](StructID structure) noexcept {
            auto fields = std::vector<ConstructionTypeRef>();
            for (const auto& field : program.declarations().structure(structure).fields) {
                fields.emplace_back(field.type);
            }
            return fields;
        }
    );
}
