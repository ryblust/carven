module carven:semantic.semir.values.impl;

import :semantic.semir.program;
import :semantic.semir.values;
import :support.invariant;
import std;

auto execution_type_reference(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> ConstructionTypeRef {
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto shape = values.generic_type_copy(*generic);
        if (const auto* canonical = std::get_if<TypeID>(&shape)) {
            return *canonical;
        }
    }
    return type;
}

auto constant_has_storage(const ConstantValueReader& values, ConstantID id) noexcept -> bool {
    const auto& fact = values.constant(id);
    return (constant_children(fact.value).has_value()
            && !std::holds_alternative<SliceConstant>(fact.value))
        || (std::holds_alternative<ByteSequenceConstant>(fact.value)
            && std::holds_alternative<VectorTypeValue>(values.type_copy(fact.type).value))
        || (std::holds_alternative<TextConstant>(fact.value)
            && values.type_copy(fact.type).value
                == CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::String}});
}

ProgramValues::ProgramValues(const SemIRProgram& program) noexcept
    : program(program) {}

auto ProgramValues::generic_type_copy(GenericTypeID) const noexcept -> GenericTypeExpression {
    invariant_violation("published execution cannot read a generic type");
}

auto ProgramValues::construction_type_copy(TypeTermID) const noexcept -> ConstructionType {
    invariant_violation("published execution cannot read a construction type");
}

auto ProgramValues::generic_member_types(GenericTypeID) const noexcept
    -> std::optional<std::vector<ConstructionTypeRef>> {
    invariant_violation("published execution cannot read generic members");
}

auto ProgramValues::callable_signature_copy(CallableSignatureID signature) const noexcept
    -> CallableSignature {
    return program.callable_signatures().signature(signature);
}

auto ProgramValues::failure_set_copy(FailureSetID failures) const noexcept -> FailureSet {
    return program.failure_sets().failure_set(failures);
}

auto ProgramValues::module_path(ModuleID module_id) const noexcept -> std::string {
    return std::string(
        program.provenance()
            .module_record(program.declarations().module_decl(module_id).provenance_module)
            .path.value()
    );
}

auto ProgramValues::struct_declaration_copy(StructID structure) const noexcept
    -> std::optional<StructDeclaration> {
    return program.declarations().structure(structure);
}

auto ProgramValues::enum_declaration_copy(EnumID enumeration) const noexcept -> EnumDeclaration {
    return program.declarations().enumeration(enumeration);
}

auto ProgramValues::enum_case_declaration_copy(EnumCaseID enum_case) const noexcept
    -> std::optional<EnumCaseDeclaration> {
    return program.declarations().enum_case(enum_case);
}

auto ProgramValues::generic_nominal_instance_copy(NominalDeclarationRef declaration) const noexcept
    -> std::optional<GenericNominalInstance> {
    const auto* instance = program.generic_nominal_instance(declaration);
    return instance ? std::optional(*instance) : std::nullopt;
}

auto ProgramValues::generic_declaration_contract_copy(
    GenericDeclarationID definition
) const noexcept -> GenericDeclarationContract {
    return program.generic_declaration_contract(definition);
}

auto ProgramValues::type_copy(TypeID type) const noexcept -> CanonicalType {
    return program.types().type(type);
}

auto ProgramValues::constant(ConstantID constant) const noexcept -> const ConstantFact& {
    return program.constants().constant(constant);
}

auto ProgramValues::spelling(ProgramSpellingID spelling) const noexcept -> std::string_view {
    return program.provenance().spelling(spelling);
}

auto ProgramValues::identity() const noexcept -> ProgramIdentity {
    return program.identity();
}

auto ProgramValues::owns(ProgramSpellingID id) const noexcept -> bool {
    return program.provenance().contains(id);
}

auto ProgramValues::builtin_type(BuiltinType type) const noexcept -> TypeID {
    return program.types().builtin_type(type);
}

auto ProgramValues::read_borrows_storage(ConstructionTypeRef type) const noexcept -> bool {
    return program.type_contents(std::get<TypeID>(type)).read_borrows_storage();
}

auto ProgramValues::struct_field_types(StructID structure) const noexcept
    -> std::optional<std::vector<TypeID>> {
    auto fields = std::vector<TypeID>();
    for (const auto& field : program.declarations().structure(structure).fields) {
        fields.push_back(field.type);
    }
    return fields;
}

auto ProgramValues::enum_case_types(EnumID enumeration) const noexcept
    -> std::optional<std::vector<EnumCaseTypes>> {
    auto result = std::vector<EnumCaseTypes>();
    for (const auto id : program.declarations().enumeration(enumeration).cases) {
        result.push_back(
            {.id = id, .payload_types = program.declarations().enum_case(id).payload_types}
        );
    }
    return result;
}

auto ProgramValues::display_names(TypeID type) const noexcept -> ExecutionDisplayNames {
    auto result = ExecutionDisplayNames {.name = {}, .is_class = false, .fields = {}, .cases = {}};
    const auto canonical = type_copy(type);
    if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
        const auto& declaration = program.declarations().structure(structure->structure);
        result.name = spelling(declaration.name);
        result.is_class = declaration.kind == RecordKind::Class;
        for (const auto& field : declaration.fields) {
            result.fields.emplace_back(spelling(field.name));
        }
    } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value)) {
        const auto& declaration = program.declarations().enumeration(enumeration->enumeration);
        result.name = spelling(declaration.name);
        for (const auto id : declaration.cases) {
            result.cases.emplace_back(id, spelling(program.declarations().enum_case(id).name));
        }
    }
    return result;
}
