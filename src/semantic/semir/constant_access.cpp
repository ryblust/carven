module carven:semantic.semir.constant_access.impl;

import :semantic.semir.constant_access;
import :semantic.semir.program;
import std;

PublishedConstantValues::PublishedConstantValues(const SemIRProgram& program) noexcept
    : program(program) {}

auto PublishedConstantValues::type_copy(TypeID type) const noexcept -> CanonicalType {
    return program.types().type(type);
}

auto PublishedConstantValues::constant(ConstantID constant) const noexcept -> const ConstantFact& {
    return program.constants().constant(constant);
}

auto PublishedConstantValues::spelling(ProgramSpellingID spelling) const noexcept
    -> std::string_view {
    return program.provenance().spelling(spelling);
}

auto PublishedConstantValues::identity() const noexcept -> ProgramIdentity {
    return program.identity();
}

auto PublishedConstantValues::owns(ProgramSpellingID id) const noexcept -> bool {
    return program.provenance().contains(id);
}
