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

auto PublishedConstantValues::builtin_type(BuiltinType type) const noexcept -> TypeID {
    return program.types().builtin_type(type);
}

auto PublishedConstantValues::read_borrows_storage(TypeID type) const noexcept -> bool {
    static_cast<void>(program.types().type(type));
    if (!contents) {
        contents = compute_type_contents(program.types(), program.declarations());
    }
    return (*contents)[type.index()].read_borrows_storage();
}

auto PublishedConstantValues::struct_field_types(StructID structure) const noexcept
    -> std::optional<std::vector<TypeID>> {
    auto fields = std::vector<TypeID>();
    for (const auto& field : program.declarations().structure(structure).fields) {
        fields.push_back(field.type);
    }
    return fields;
}

auto PublishedConstantValues::enum_case_types(EnumID enumeration) const noexcept
    -> std::optional<std::vector<EnumCaseTypes>> {
    auto result = std::vector<EnumCaseTypes>();
    for (const auto id : program.declarations().enumeration(enumeration).cases) {
        result.push_back(
            {.id = id, .payload_types = program.declarations().enum_case(id).payload_types}
        );
    }
    return result;
}
