module carven:semantic.analysis.generics.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.generic;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto generic_contract(const GenericDeclarationDefinition& definition) noexcept
    -> const GenericDeclarationContract& {
    return definition.visit([](const auto& value) noexcept -> const GenericDeclarationContract& {
        return value.contract;
    });
}

auto generic_fields(const GenericDeclarationDefinition& definition) noexcept
    -> std::vector<GenericTypeID> {
    auto result = std::vector<GenericTypeID>();
    definition.visit(
        Overloaded {
            [&](const GenericRecordDefinition& value) noexcept {
                for (const auto& field : value.fields) {
                    result.push_back(field.type);
                }
            },
            [&](const GenericFunctionDefinition& value) noexcept {
                for (const auto& parameter : value.signature.parameters) {
                    if (const auto* type = std::get_if<GenericTypeID>(&parameter.type)) {
                        result.push_back(*type);
                    }
                }
                if (const auto* type = std::get_if<GenericTypeID>(&value.signature.result)) {
                    result.push_back(*type);
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
    GenericDeclarationDefinition definition
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
    -> GenericDeclarationDefinition {
    if (id.owner() != program_identity
        || id.index() >= storage.generic_definitions.size()
        || !storage.generic_definitions[id.index()]) {
        invariant_violation("generic declaration query requires a completed source head");
    }
    return *storage.generic_definitions[id.index()];
}

auto ProgramDraft::intern_generic_type(GenericTypeExpression expression) noexcept -> GenericTypeID {
    if (const auto* tuple = std::get_if<GenericTupleType>(&expression);
        tuple && tuple->elements.empty()) {
        expression = builtin_type(BuiltinType::Void);
    }
    const auto validate_child = [&](ConstructionTypeRef type) noexcept {
        type.visit([&](auto id) noexcept {
            using ID = decltype(id);
            if constexpr (std::same_as<ID, TypeID>) {
                static_cast<void>(type_copy(id));
            } else if constexpr (std::same_as<ID, TypeTermID>) {
                static_cast<void>(construction_type_copy(id));
            } else {
                static_cast<void>(generic_type_copy(id));
            }
        });
    };
    expression.visit(
        Overloaded {
            [&](TypeID id) noexcept { static_cast<void>(type_copy(id)); },
            [&](const GenericTypeParameter& parameter) noexcept {
                if (parameter.definition.owner() != program_identity
                    || parameter.definition.index() >= storage.generic_definitions.size()) {
                    invariant_violation("generic type parameter has no declaration identity");
                }
            },
            [&](const GenericArrayType& array) noexcept { validate_child(array.element); },
            [&](const GenericTupleType& tuple) noexcept {
                for (const auto element : tuple.elements) {
                    validate_child(element);
                }
            },
            [&](const GenericSliceType& slice) noexcept { validate_child(slice.element); },
            [&](const GenericOwnedSequenceType& sequence) noexcept {
                validate_child(sequence.element);
            },
            [&](const GenericPointerType& pointer) noexcept { validate_child(pointer.target); },
            [&](const GenericOperationType& operation) noexcept {
                validate_child(operation.success);
                if (operation.failures.visit([](auto id) static noexcept { return id.owner(); })
                    != program_identity) {
                    invariant_violation("generic operation used a foreign failure expression");
                }
            },
            [&](const GenericNominalApplication& application) noexcept {
                if (application.definition.owner() != program_identity
                    || application.definition.index() >= storage.generic_definitions.size()) {
                    invariant_violation("generic application has no declaration identity");
                }
                for (const auto& argument : application.arguments) {
                    visit_generic_argument_types(argument, validate_child);
                    visit_generic_argument_failures(
                        argument,
                        [&](ConstructionFailureRef effect) noexcept {
                            if (effect.visit([](auto id) static noexcept { return id.owner(); })
                                != program_identity) {
                                invariant_violation(
                                    "generic application used a foreign failure argument"
                                );
                            }
                        }
                    );
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

auto ProgramDraft::substitute_generic_environment(
    std::span<const GenericArgument> source,
    GenericDeclarationID definition,
    std::span<const GenericArgument> arguments,
    FailureInstantiation* effects
) noexcept -> GenericEnvironment {
    if (!effects) {
        auto session = make_failure_instantiation(definition, arguments);
        return substitute_generic_environment(source, definition, arguments, &session);
    }

    auto result = GenericEnvironment();
    for (const auto& argument : source) {
        argument.visit([&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, ConstructionTypeRef>) {
                result.emplace_back(substitute_generic_type(value, definition, arguments, effects));
            } else if constexpr (std::same_as<Value, ConstructionFailureRef>) {
                result.emplace_back(effects->substitute(value));
            }
        });
    }
    return result;
}

auto ProgramDraft::substitute_generic_type(
    ConstructionTypeRef type,
    GenericDeclarationID definition,
    std::span<const GenericArgument> arguments,
    FailureInstantiation* effects
) noexcept -> ConstructionTypeRef {
    if (!effects) {
        auto session = make_failure_instantiation(definition, arguments);
        return substitute_generic_type(type, definition, arguments, &session);
    }
    const auto* generic = std::get_if<GenericTypeID>(&type);
    if (!generic) {
        const auto* term = std::get_if<TypeTermID>(&type);
        if (!term) {
            return type;
        }
        auto shape = construction_type_copy(*term);
        const auto child = [&](ConstructionTypeRef value) noexcept {
            return substitute_generic_type(value, definition, arguments, effects);
        };
        return shape.value.visit([&](auto& value) noexcept -> ConstructionTypeRef {
            using Shape = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Shape, ConstructionArrayTypeValue>) {
                return make_array_type(*this, child(value.element), value.extent);
            } else if constexpr (std::same_as<Shape, ConstructionTupleTypeValue>) {
                for (auto& element : value.elements) {
                    element = child(element);
                }
                return make_tuple_type(*this, std::move(value.elements));
            } else if constexpr (std::same_as<Shape, ConstructionSliceTypeValue>) {
                return make_slice_type(*this, child(value.element));
            } else if constexpr (std::same_as<Shape, ConstructionOperationTypeValue>) {
                return make_operation_type(
                    *this,
                    child(value.success),
                    effects->substitute(value.failures)
                );
            } else {
                static_assert(std::same_as<Shape, ConstructionCallableViewTypeValue>);
                for (auto& parameter : value.parameters) {
                    parameter.type = child(parameter.type);
                }
                value.result = child(value.result);
                value.failures = effects->substitute(value.failures);
                return append_construction_type(std::move(shape));
            }
        });
    }
    auto shape = generic_type_copy(*generic);
    if (const auto* concrete = std::get_if<TypeID>(&shape)) {
        return *concrete;
    }
    if (const auto* parameter = std::get_if<GenericTypeParameter>(&shape)) {
        if (parameter->definition != definition) {
            return type;
        }
        if (parameter->index >= arguments.size()) {
            invariant_violation("generic substitution has an incomplete parameter environment");
        }
        return std::get<ConstructionTypeRef>(arguments[parameter->index]);
    }

    const auto child = [&](ConstructionTypeRef& type) noexcept {
        type = substitute_generic_type(type, definition, arguments, effects);
    };
    shape.visit([&](auto& value) noexcept {
        using Shape = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Shape, GenericArrayType>
                      || std::same_as<Shape, GenericSliceType>
                      || std::same_as<Shape, GenericOwnedSequenceType>) {
            child(value.element);
        } else if constexpr (std::same_as<Shape, GenericTupleType>) {
            for (auto& element : value.elements) {
                child(element);
            }
        } else if constexpr (std::same_as<Shape, GenericPointerType>) {
            child(value.target);
        } else if constexpr (std::same_as<Shape, GenericOperationType>) {
            child(value.success);
            value.failures = effects->substitute(value.failures);
        } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
            value.arguments =
                substitute_generic_environment(value.arguments, definition, arguments, effects);
        }
    });
    return intern_generic_type(std::move(shape));
}

auto ProgramDraft::resolve_generic_type(
    ConstructionTypeRef type,
    GenericDeclarationID definition,
    std::span<const CanonicalGenericArgument> arguments,
    ProgramOriginID origin,
    FailureInstantiation* effects
) noexcept -> AnalysisResult<TypeID> {
    if (!effects) {
        auto session = make_failure_instantiation(definition, generic_environment(arguments));
        return resolve_generic_type(type, definition, arguments, origin, &session);
    }
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        return *concrete;
    }
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        if (const auto closed = try_canonicalize_declared_type(*term)) {
            return *closed;
        }
        return std::unexpected(
            diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::TypeGenericArguments,
                    "this concrete type boundary requires a determined type and failure set"
                )
                    .primary(source_span(origin))
                    .build()
            )
        );
    }
    return generic_type_copy(std::get<GenericTypeID>(type))
        .visit(
            Overloaded {
                [](TypeID concrete) static noexcept -> AnalysisResult<TypeID> { return concrete; },
                [&](const GenericTypeParameter& parameter) noexcept -> AnalysisResult<TypeID> {
                    if (parameter.definition != definition || parameter.index >= arguments.size()) {
                        invariant_violation(
                            "concrete generic resolution requires the declaring parameter environment"
                        );
                    }
                    return std::get<TypeID>(arguments[parameter.index]);
                },
                [&](const GenericArrayType& array) noexcept -> AnalysisResult<TypeID> {
                    auto element =
                        resolve_generic_type(array.element, definition, arguments, origin, effects);
                    if (!element) {
                        return std::unexpected(element.error());
                    }
                    return intern_type({.value = ArrayTypeValue {*element, array.extent}});
                },
                [&](const GenericTupleType& tuple) noexcept -> AnalysisResult<TypeID> {
                    auto result = TupleTypeValue {.elements = {}};
                    for (const auto element : tuple.elements) {
                        auto resolved =
                            resolve_generic_type(element, definition, arguments, origin, effects);
                        if (!resolved) {
                            return std::unexpected(resolved.error());
                        }
                        result.elements.push_back(*resolved);
                    }
                    return intern_type({.value = std::move(result)});
                },
                [&](const GenericSliceType& slice) noexcept -> AnalysisResult<TypeID> {
                    auto element =
                        resolve_generic_type(slice.element, definition, arguments, origin, effects);
                    if (!element) {
                        return std::unexpected(element.error());
                    }
                    return intern_type({.value = SliceTypeValue {*element}});
                },
                [&](const GenericOwnedSequenceType& sequence) noexcept -> AnalysisResult<TypeID> {
                    auto element = resolve_generic_type(
                        sequence.element,
                        definition,
                        arguments,
                        origin,
                        effects
                    );
                    if (!element) {
                        return std::unexpected(element.error());
                    }
                    record_sequence_element(*element, origin);
                    return intern_type({.value = OwnedSequenceTypeValue {*element}});
                },
                [&](const GenericPointerType& pointer) noexcept -> AnalysisResult<TypeID> {
                    auto target = resolve_generic_type(
                        pointer.target,
                        definition,
                        arguments,
                        origin,
                        effects
                    );
                    if (!target) {
                        return std::unexpected(target.error());
                    }
                    return intern_type({.value = PointerTypeValue {*target, pointer.access}});
                },
                [&](const GenericOperationType& operation) noexcept -> AnalysisResult<TypeID> {
                    auto success = resolve_generic_type(
                        operation.success,
                        definition,
                        arguments,
                        origin,
                        effects
                    );
                    if (!success) {
                        return std::unexpected(success.error());
                    }
                    const auto changed_failures = effects->substitute(operation.failures);
                    const auto* failures = std::get_if<FailureSetID>(&changed_failures);
                    if (!failures) {
                        return std::unexpected(
                            diagnostics().error(
                                DiagnosticBuilder(
                                    DiagnosticCode::TypeGenericArguments,
                                    "this concrete type boundary requires a determined failure set"
                                )
                                    .primary(source_span(origin))
                                    .build()
                            )
                        );
                    }
                    return intern_type({.value = OperationTypeValue {*success, *failures}});
                },
                [&](const GenericNominalApplication& application) noexcept
                    -> AnalysisResult<TypeID> {
                    auto resolved_arguments = CanonicalGenericEnvironment();
                    const auto applied = substitute_generic_environment(
                        application.arguments,
                        definition,
                        generic_environment(arguments),
                        effects
                    );
                    for (const auto& argument : applied) {
                        if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                            auto resolved =
                                resolve_generic_type(*type, definition, arguments, origin, effects);
                            if (!resolved) {
                                return std::unexpected(resolved.error());
                            }
                            resolved_arguments.emplace_back(*resolved);
                        } else if (const auto* effect =
                                       std::get_if<ConstructionFailureRef>(&argument)) {
                            const auto changed = effects->substitute(*effect);
                            const auto* closed = std::get_if<FailureSetID>(&changed);
                            if (!closed) {
                                return std::unexpected(
                                    diagnostics().error(
                                        DiagnosticBuilder(
                                            DiagnosticCode::TypeGenericArguments,
                                            "a nominal instance requires a closed failure-set argument"
                                        )
                                            .primary(source_span(origin))
                                            .build()
                                    )
                                );
                            }
                            resolved_arguments.emplace_back(*closed);
                        }
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
    std::span<const CanonicalGenericArgument> arguments,
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
    if (std::holds_alternative<GenericFunctionDefinition>(source)) {
        return std::unexpected(
            diagnostics().error(DiagnosticBuilder(
                                    DiagnosticCode::TypeGenericArguments,
                                    "a generic function does not name a type"
            )
                                    .primary(source_span(origin))
                                    .build())
        );
    }
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
    for (const auto [index, argument] : std::views::enumerate(arguments)) {
        const auto type = std::get_if<TypeID>(&argument);
        if (!generic_argument_matches(contract.parameter_kinds[index], argument)) {
            return std::unexpected(
                diagnostics().error(DiagnosticBuilder(
                                        DiagnosticCode::TypeGenericArguments,
                                        "generic argument kind differs from its parameter"
                )
                                        .primary(source_span(origin))
                                        .build())
            );
        }

        if (!type && std::holds_alternative<FailureSetID>(argument)) {
            for (const auto member : failure_set_copy(std::get<FailureSetID>(argument)).members) {
                auto admitted = require_failure_member(member, origin);
                if (!admitted) {
                    return std::unexpected(admitted.error());
                }
            }
            continue;
        }

        const auto admitted = require_source_value_type(
            *this,
            *type,
            source_module(application_source.source_id),
            application_source.span,
            "generic type argument"
        );
        if (!admitted) {
            return std::unexpected(admitted.error());
        }
    }
    const auto key =
        std::pair(definition, CanonicalGenericEnvironment(arguments.begin(), arguments.end()));
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
            [](const GenericFunctionDefinition&) static noexcept -> NominalDeclarationRef {
                invariant_violation("generic function cannot reserve a nominal instance");
            },
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
    auto effects = make_failure_instantiation(definition, generic_environment(key.second));
    // The owned key keeps this environment stable while nested instances grow storage.
    auto completion = source.visit(
        Overloaded {
            [](const GenericFunctionDefinition&) static noexcept -> AnalysisResult<void> {
                invariant_violation("generic function cannot complete a nominal instance");
            },
            [&](const GenericRecordDefinition& record) noexcept -> AnalysisResult<void> {
                auto fields = std::vector<ConstructionStructField>();
                for (const auto& field : record.fields) {
                    auto resolved = resolve_generic_type(
                        field.type,
                        definition,
                        key.second,
                        field.origin,
                        &effects
                    );
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
                        auto resolved = resolve_generic_type(
                            type,
                            definition,
                            key.second,
                            member.origin,
                            &effects
                        );
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
