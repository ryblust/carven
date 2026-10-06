module carven:semantic.analysis.generics.queries.impl;

import :semantic.analysis.program;
import :semantic.semir.generic;
import std;

auto ProgramDraft::generic_nominal_instance_copy(NominalDeclarationRef declaration) const noexcept
    -> std::optional<GenericNominalInstance> {
    const auto found = storage.generic_instance_nominals.find(declaration);
    return found == storage.generic_instance_nominals.end()
        ? std::nullopt
        : std::optional(storage.generic_instances[found->second].instance);
}

auto ProgramDraft::generic_declaration_contract_copy(GenericDeclarationID definition) const noexcept
    -> GenericDeclarationContract {
    return generic_declaration_copy(definition).visit([](const auto& source) noexcept {
        return source.contract;
    });
}
