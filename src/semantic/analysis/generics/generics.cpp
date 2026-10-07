module carven:semantic.analysis.generics.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.generic;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto generic_contract(const GenericNominalDefinition& definition) noexcept
    -> const GenericDeclarationContract& {
    return definition.visit([](const auto& value) noexcept -> const GenericDeclarationContract& {
        return value.contract;
    });
}

auto generic_fields(const GenericNominalDefinition& definition) noexcept
    -> std::vector<GenericTypeID> {
    auto result = std::vector<GenericTypeID>();
    definition.visit(
        Overloaded {
            [&](const GenericRecordDefinition& value) noexcept {
                for (const auto& field : value.fields) {
                    result.push_back(field.type);
                }
            },
            [&](const GenericEnumDefinition& value) noexcept {
                for (const auto& member : value.cases) {
                    result.insert(
                        result.end(),
                        member.payload_types.begin(),
                        member.payload_types.end()
                    );
                }
            },
        }
    );
    return result;
}

auto nominal_type(ProgramDraft& draft, NominalDeclarationRef declaration) noexcept -> TypeID {
    return declaration.visit(
        Overloaded {
            [&](StructID id) noexcept {
                return draft.intern_type({.value = StructTypeValue {id}});
            },
            [&](EnumID id) noexcept { return draft.intern_type({.value = EnumTypeValue {id}}); },
        }
    );
}

} // namespace

auto ProgramDraft::reserve_generic_declaration() noexcept -> GenericDeclarationID {
    require_state(State::Declarations, "reserve generic source declaration");
    if (storage.generic_definitions.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("generic declaration identities exhausted");
    }
    const auto id = GenericDeclarationID(
        program_identity,
        static_cast<std::uint32_t>(storage.generic_definitions.size())
    );
    storage.generic_definitions.emplace_back(std::nullopt);
    return id;
}

auto ProgramDraft::define_generic_declaration(
    GenericDeclarationID id,
    GenericNominalDefinition definition
) noexcept -> void {
    require_state(State::Declarations, "define generic source declaration");
    if (id.owner() != program_identity
        || id.index() >= storage.generic_definitions.size()
        || storage.generic_definitions[id.index()]) {
        invariant_violation("generic definition disagrees with its reservation");
    }
    const auto& contract = generic_contract(definition);
    if (contract.module_id.owner() != program_identity
        || !owns(contract.name)
        || !owns(contract.origin)
        || contract.parameters.empty()) {
        invariant_violation("generic definition has invalid source contract");
    }
    for (const auto name : contract.parameters) {
        if (!owns(name)) {
            invariant_violation("generic parameter spelling belongs to another program");
        }
    }
    for (const auto type : generic_fields(definition)) {
        static_cast<void>(generic_type_copy(type));
    }
    storage.generic_definitions[id.index()] = std::move(definition);
}

auto ProgramDraft::generic_declaration_copy(GenericDeclarationID id) const noexcept
    -> GenericNominalDefinition {
    if (id.owner() != program_identity
        || id.index() >= storage.generic_definitions.size()
        || !storage.generic_definitions[id.index()]) {
        invariant_violation("generic declaration query requires a completed source head");
    }
    return *storage.generic_definitions[id.index()];
}

auto ProgramDraft::intern_generic_type(GenericTypeExpression expression) noexcept -> GenericTypeID {
    expression.visit(
        Overloaded {
            [&](TypeID id) noexcept { static_cast<void>(type_copy(id)); },
            [&](const GenericTypeParameter& parameter) noexcept {
                if (parameter.definition.owner() != program_identity
                    || parameter.definition.index() >= storage.generic_definitions.size()) {
                    invariant_violation("generic type parameter has no declaration identity");
                }
            },
            [&](const GenericArrayType& array) noexcept {
                static_cast<void>(generic_type_copy(array.element));
            },
            [&](const GenericSliceType& slice) noexcept {
                static_cast<void>(generic_type_copy(slice.element));
            },
            [&](const GenericOwnedSequenceType& sequence) noexcept {
                static_cast<void>(generic_type_copy(sequence.element));
            },
            [&](const GenericPointerType& pointer) noexcept {
                static_cast<void>(generic_type_copy(pointer.target));
            },
            [&](const GenericNominalApplication& application) noexcept {
                if (application.definition.owner() != program_identity
                    || application.definition.index() >= storage.generic_definitions.size()) {
                    invariant_violation("generic application has no declaration identity");
                }
                for (const auto type : application.arguments) {
                    static_cast<void>(generic_type_copy(type));
                }
            },
        }
    );
    if (const auto found = storage.generic_type_index.find(expression);
        found != storage.generic_type_index.end()) {
        return found->second;
    }
    if (storage.generic_types.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("generic type expression identities exhausted");
    }
    const auto id =
        GenericTypeID(program_identity, static_cast<std::uint32_t>(storage.generic_types.size()));
    storage.generic_type_index.emplace(expression, id);
    storage.generic_types.push_back(std::move(expression));
    return id;
}

auto ProgramDraft::generic_type_copy(GenericTypeID id) const noexcept -> GenericTypeExpression {
    if (id.owner() != program_identity || id.index() >= storage.generic_types.size()) {
        invariant_violation("generic type expression belongs to another program");
    }
    return storage.generic_types[id.index()];
}

auto ProgramDraft::substitute_generic_type(
    GenericTypeID type,
    GenericDeclarationID definition,
    std::span<const GenericTypeID> arguments
) noexcept -> GenericTypeID {
    return generic_type_copy(type).visit(
        Overloaded {
            [&](TypeID) noexcept { return type; },
            [&](const GenericTypeParameter& parameter) noexcept {
                if (parameter.definition != definition) {
                    return type;
                }
                if (parameter.index >= arguments.size()) {
                    invariant_violation(
                        "generic substitution has an incomplete parameter environment"
                    );
                }
                return arguments[parameter.index];
            },
            [&](const GenericArrayType& array) noexcept {
                return intern_generic_type(
                    GenericArrayType {
                        substitute_generic_type(array.element, definition, arguments),
                        array.extent
                    }
                );
            },
            [&](const GenericSliceType& slice) noexcept {
                return intern_generic_type(
                    GenericSliceType {substitute_generic_type(slice.element, definition, arguments)}
                );
            },
            [&](const GenericOwnedSequenceType& sequence) noexcept {
                return intern_generic_type(
                    GenericOwnedSequenceType {
                        substitute_generic_type(sequence.element, definition, arguments)
                    }
                );
            },
            [&](const GenericPointerType& pointer) noexcept {
                return intern_generic_type(
                    GenericPointerType {
                        substitute_generic_type(pointer.target, definition, arguments),
                        pointer.access
                    }
                );
            },
            [&](const GenericNominalApplication& application) noexcept {
                auto result = GenericNominalApplication {application.definition, {}};
                for (const auto argument : application.arguments) {
                    result.arguments.push_back(
                        substitute_generic_type(argument, definition, arguments)
                    );
                }
                return intern_generic_type(std::move(result));
            },
        }
    );
}

auto ProgramDraft::resolve_generic_type(
    GenericTypeID type,
    GenericDeclarationID definition,
    std::span<const TypeID> arguments,
    ProgramOriginID origin
) noexcept -> AnalysisResult<TypeID> {
    return generic_type_copy(type).visit(
        Overloaded {
            [](TypeID concrete) static noexcept -> AnalysisResult<TypeID> { return concrete; },
            [&](const GenericTypeParameter& parameter) noexcept -> AnalysisResult<TypeID> {
                if (parameter.definition != definition || parameter.index >= arguments.size()) {
                    invariant_violation(
                        "concrete generic resolution requires the declaring parameter environment"
                    );
                }
                return arguments[parameter.index];
            },
            [&](const GenericArrayType& array) noexcept -> AnalysisResult<TypeID> {
                auto element = resolve_generic_type(array.element, definition, arguments, origin);
                if (!element) {
                    return std::unexpected(element.error());
                }
                return intern_type({.value = ArrayTypeValue {*element, array.extent}});
            },
            [&](const GenericSliceType& slice) noexcept -> AnalysisResult<TypeID> {
                auto element = resolve_generic_type(slice.element, definition, arguments, origin);
                if (!element) {
                    return std::unexpected(element.error());
                }
                return intern_type({.value = SliceTypeValue {*element}});
            },
            [&](const GenericOwnedSequenceType& sequence) noexcept -> AnalysisResult<TypeID> {
                auto element =
                    resolve_generic_type(sequence.element, definition, arguments, origin);
                if (!element) {
                    return std::unexpected(element.error());
                }
                record_sequence_element(*element, origin);
                return intern_type({.value = OwnedSequenceTypeValue {*element}});
            },
            [&](const GenericPointerType& pointer) noexcept -> AnalysisResult<TypeID> {
                auto target = resolve_generic_type(pointer.target, definition, arguments, origin);
                if (!target) {
                    return std::unexpected(target.error());
                }
                return intern_type({.value = PointerTypeValue {*target, pointer.access}});
            },
            [&](const GenericNominalApplication& application) noexcept -> AnalysisResult<TypeID> {
                auto resolved_arguments = std::vector<TypeID>();
                for (const auto argument : application.arguments) {
                    auto resolved = resolve_generic_type(argument, definition, arguments, origin);
                    if (!resolved) {
                        return std::unexpected(resolved.error());
                    }
                    resolved_arguments.push_back(*resolved);
                }
                return instantiate_generic_nominal(
                    application.definition,
                    resolved_arguments,
                    origin
                );
            },
        }
    );
}

auto ProgramDraft::instantiate_generic_nominal(
    GenericDeclarationID definition,
    std::span<const TypeID> arguments,
    ProgramOriginID origin
) noexcept -> AnalysisResult<TypeID> {
    if (!owns(origin)) {
        invariant_violation("generic application used a foreign origin");
    }
    if (definition.owner() != program_identity
        || definition.index() >= storage.generic_definitions.size()) {
        invariant_violation("generic instance refers to an unreserved definition");
    }
    if (!storage.generic_definitions[definition.index()]) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeGenericDefinition,
            "generic declaration head is not complete"
        );
        diagnostic.primary(
            source_span(origin),
            "this application depends on an unfinished generic definition"
        );
        return std::unexpected(diagnostics().error(diagnostic.build()));
    }
    const auto checked = validate_generic_definitions(definition);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    const auto source = generic_declaration_copy(definition);
    const auto& contract = generic_contract(source);
    if (arguments.size() != contract.parameters.size()) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeGenericArguments,
            "generic type argument count differs from its declaration"
        );
        diagnostic.primary(source_span(origin), "supply one type argument for each parameter");
        diagnostic.related(source_span(contract.origin), "generic declaration");
        return std::unexpected(diagnostics().error(diagnostic.build()));
    }
    const auto application_source = source_origin(origin);
    for (const auto argument : arguments) {
        const auto admitted = require_source_value_type(
            *this,
            argument,
            source_module(application_source.source_id),
            application_source.span,
            "generic type argument"
        );
        if (!admitted) {
            return std::unexpected(admitted.error());
        }
    }
    const auto key = std::pair(definition, std::vector<TypeID>(arguments.begin(), arguments.end()));
    if (const auto found = storage.generic_instance_index.find(key);
        found != storage.generic_instance_index.end()) {
        const auto& slot = storage.generic_instances[found->second];
        if (const auto* failure = std::get_if<AnalysisFailure>(&slot.state)) {
            return std::unexpected(*failure);
        }
        return nominal_type(*this, slot.instance.declaration);
    }
    if (storage.generic_instances.size() >= maximum_generic_instances
        || storage.generic_instance_depth >= maximum_generic_depth) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeGenericLimits,
            storage.generic_instance_depth >= maximum_generic_depth
                ? "generic instance construction exceeded its depth budget"
                : "generic instance construction exceeded its instance budget"
        );
        diagnostic.primary(
            source_span(origin),
            "the compiler's generic construction budget was exhausted"
        );
        return std::unexpected(diagnostics().error(diagnostic.build()));
    }

    struct InstanceDepth final {
        std::size_t& depth;

        ~InstanceDepth() { --depth; }
    };

    ++storage.generic_instance_depth;
    const auto active_depth = InstanceDepth {storage.generic_instance_depth};
    // Normalized arguments select one semantic declaration, including recursive
    // references made while its fields are being completed.
    const auto declaration = source.visit(
        Overloaded {
            [&](const GenericRecordDefinition&) noexcept -> NominalDeclarationRef {
                return storage.declarations.reserve_struct();
            },
            [&](const GenericEnumDefinition&) noexcept -> NominalDeclarationRef {
                return storage.declarations.reserve_enum();
            },
        }
    );
    const auto index = storage.generic_instances.size();
    storage.generic_instance_index.emplace(key, index);
    storage.generic_instance_nominals.emplace(declaration, index);
    storage.generic_instances.push_back({
        .instance = {.definition = definition, .arguments = key.second, .declaration = declaration},
        .state = ConstructionStorage::GenericInstanceSlot::Progress::Instantiating,
    });
    // The owned key keeps this environment stable while nested instances grow storage.
    auto completion = source.visit(
        Overloaded {
            [&](const GenericRecordDefinition& record) noexcept -> AnalysisResult<void> {
                auto fields = std::vector<ConstructionStructField>();
                for (const auto& field : record.fields) {
                    auto resolved =
                        resolve_generic_type(field.type, definition, key.second, field.origin);
                    if (!resolved) {
                        return std::unexpected(resolved.error());
                    }
                    fields.push_back(
                        {.name = field.name, .type = *resolved, .origin = field.origin}
                    );
                }
                storage.declarations.define(
                    std::get<StructID>(declaration),
                    ConstructionStructDeclaration {
                        .kind = record.kind,
                        .module_id = contract.module_id,
                        .name = contract.name,
                        .origin = contract.origin,
                        .visibility = DeclarationVisibility::Module,
                        .fields = std::move(fields),
                    }
                );
                return {};
            },
            [&](const GenericEnumDefinition& enumeration) noexcept -> AnalysisResult<void> {
                const auto owner = std::get<EnumID>(declaration);
                auto cases = std::vector<EnumCaseID>();
                for (const auto& member : enumeration.cases) {
                    const auto id = storage.declarations.reserve_enum_case();
                    auto payload = std::vector<ConstructionTypeRef>();
                    for (const auto type : member.payload_types) {
                        auto resolved =
                            resolve_generic_type(type, definition, key.second, member.origin);
                        if (!resolved) {
                            return std::unexpected(resolved.error());
                        }
                        payload.push_back(*resolved);
                    }
                    auto constant = std::optional<ConstantID>();
                    const auto enum_type = nominal_type(*this, declaration);
                    if (member.payload_types.empty()) {
                        constant = intern_constant(
                            {.type = enum_type,
                             .value = PayloadEnumConstant {.enum_case = id, .payload = {}}}
                        );
                    }
                    storage.declarations.define(
                        id,
                        ConstructionEnumCaseDeclaration {
                            .owner = owner,
                            .name = member.name,
                            .origin = member.origin,
                            .payload_types = std::move(payload),
                            .constant = constant,
                        }
                    );
                    cases.push_back(id);
                }
                storage.declarations.define(
                    owner,
                    EnumDeclaration {
                        .module_id = contract.module_id,
                        .name = contract.name,
                        .origin = contract.origin,
                        .visibility = DeclarationVisibility::Module,
                        .representation = PayloadEnumRepresentation {},
                        .cases = std::move(cases),
                        .supports_equality = false,
                    }
                );
                return {};
            },
        }
    );
    if (!completion) {
        storage.generic_instances[index].state = completion.error();
        return std::unexpected(completion.error());
    }
    storage.generic_instances[index].state =
        ConstructionStorage::GenericInstanceSlot::Progress::Complete;
    return nominal_type(*this, declaration);
}
