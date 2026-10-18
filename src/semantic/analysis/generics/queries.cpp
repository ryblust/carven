module carven:semantic.analysis.generics.queries.impl;

import :semantic.analysis.generics.snapshot;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.generic;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

auto ProgramDraft::reserve_generic_body(GenericDeclarationID definition) noexcept
    -> BodyReservation {
    const auto source = generic_declaration_copy(definition);
    if (!std::holds_alternative<GenericFunctionDefinition>(source)
        || has_generic_function_body(definition)) {
        invariant_violation("generic body reservation requires an unchecked function definition");
    }
    return BodyReservation(
        BodyID(program_identity, definition.index()),
        BodyKind::Function,
        provenance_identity(),
        BodyIdentityDomain::GenericDefinition
    );
}

auto ProgramDraft::set_generic_function_body(
    GenericDeclarationID definition,
    StructuredBodyDraft body
) noexcept -> void {
    body.generic_definition = definition;
    if (definition.owner() != program_identity
        || body.bindings.owner()
            != BodyIdentity(
                program_identity,
                definition.index(),
                BodyIdentityDomain::GenericDefinition
            )
        || !storage.generic_function_bodies
                .emplace(definition, std::make_unique<StructuredBodyDraft>(std::move(body)))
                .second) {
        invariant_violation("checked generic body disagrees with its definition reservation");
    }
    // Newly checked call edges participate in the same recursive parameter-flow graph.
    storage.checked_generic_definitions.clear();
}

auto ProgramDraft::has_generic_function_body(GenericDeclarationID definition) const noexcept
    -> bool {
    return storage.generic_function_bodies.contains(definition);
}

auto ProgramDraft::generic_function_body(GenericDeclarationID definition) const noexcept
    -> const StructuredBodyDraft& {
    const auto found = storage.generic_function_bodies.find(definition);
    if (found == storage.generic_function_bodies.end()) {
        invariant_violation("generic function body requires completed definition checking");
    }
    return *found->second;
}

auto ProgramDraft::generic_nominal_instance_copy(NominalDeclarationRef declaration) const noexcept
    -> std::optional<GenericNominalInstance> {
    const auto found = storage.generic_instance_nominals.find(declaration);
    return found == storage.generic_instance_nominals.end()
        ? std::nullopt
        : std::optional(storage.generic_instances[found->second].instance);
}

auto ProgramDraft::generic_declaration_contract_copy(GenericDeclarationID definition) const noexcept
    -> GenericDeclarationContract {
    return generic_declaration_copy(definition).visit([](const auto& source) static noexcept {
        return source.contract;
    });
}

auto ProgramDraft::generic_declaration_ids() const noexcept -> std::vector<GenericDeclarationID> {
    auto ids = std::vector<GenericDeclarationID>();
    for (auto i = 0uz; i < storage.generic_definitions.size(); ++i) {
        ids.push_back(GenericDeclarationID(program_identity, static_cast<std::uint32_t>(i)));
    }
    return ids;
}

auto ProgramDraft::generic_parameter_environment(GenericDeclarationID definition) noexcept
    -> GenericEnvironment {
    const auto contract = generic_declaration_contract_copy(definition);
    auto result = GenericEnvironment();
    for (const auto [index, policy] : std::views::enumerate(contract.parameter_kinds)) {
        if (policy == GenericParameterKind::Type) {
            result.emplace_back(ConstructionTypeRef(intern_generic_type(
                GenericTypeParameter {
                    .definition = definition,
                    .index = static_cast<std::uint32_t>(index)
                }
            )));
        } else {
            result.emplace_back(ConstructionFailureRef(
                add_generic_failure_parameter({definition, static_cast<std::uint32_t>(index)})
            ));
        }
    }
    return result;
}

auto ProgramDraft::complete_generic_function_result(
    GenericDeclarationID definition,
    ConstructionTypeRef result
) noexcept -> void {
    auto& source =
        std::get<GenericFunctionDefinition>(*storage.generic_definitions.at(definition.index()));
    if (!source.infer_result) {
        invariant_violation("generic result completed without inference");
    }
    source.signature.result = result;
    source.infer_result = false;
}

auto ProgramDraft::generic_member_types(GenericTypeID type) const noexcept
    -> std::optional<std::vector<ConstructionTypeRef>> {
    if (const auto found = storage.generic_record_shapes.find(type);
        found != storage.generic_record_shapes.end()) {
        return found->second;
    }
    if (const auto found = storage.generic_enum_shapes.find(type);
        found != storage.generic_enum_shapes.end()) {
        auto fields = std::vector<ConstructionTypeRef>();
        for (const auto& payload : found->second) {
            fields.insert(fields.end(), payload.begin(), payload.end());
        }
        return fields;
    }
    return std::nullopt;
}

auto ProgramDraft::cache_generic_record_fields(
    GenericTypeID type,
    const std::vector<ConstructionTypeRef>& fields
) noexcept -> void {
    storage.generic_record_shapes.try_emplace(type, fields);
}

auto ProgramDraft::prepare_generic_analysis_shapes() noexcept -> void {
    // Expanding an effect-bearing field may clone constraint terms. Complete
    // every shape while the construction graph is still open, never at seal.
    for (auto index = 0uz; index < storage.generic_types.size(); ++index) {
        const auto type = GenericTypeID(program_identity, static_cast<std::uint32_t>(index));
        if (storage.generic_record_shapes.contains(type)
            || storage.generic_enum_shapes.contains(type)) {
            continue;
        }
        const auto shape = generic_type_copy(type);
        const auto* application = std::get_if<GenericNominalApplication>(&shape);
        if (!application) {
            continue;
        }
        const auto source = generic_declaration_copy(application->definition);
        auto effects = make_failure_instantiation(application->definition, application->arguments);
        if (const auto* record = std::get_if<GenericRecordDefinition>(&source)) {
            auto fields = std::vector<ConstructionTypeRef>();
            for (const auto& field : record->fields) {
                fields.push_back(substitute_generic_type(
                    field.type,
                    application->definition,
                    application->arguments,
                    &effects
                ));
            }
            storage.generic_record_shapes.emplace(type, std::move(fields));
        } else if (const auto* enumeration = std::get_if<GenericEnumDefinition>(&source)) {
            auto cases = std::vector<std::vector<ConstructionTypeRef>>();
            for (const auto& item : enumeration->cases) {
                auto fields = std::vector<ConstructionTypeRef>();
                for (const auto field : item.payload_types) {
                    fields.push_back(substitute_generic_type(
                        field,
                        application->definition,
                        application->arguments,
                        &effects
                    ));
                }
                cases.push_back(std::move(fields));
            }
            storage.generic_enum_shapes.emplace(type, std::move(cases));
        }
    }
}

auto ProgramDraft::take_generic_analysis_snapshot(
    const TypeResolution& types,
    std::shared_ptr<const FailureSolution> failures
) noexcept -> GenericAnalysisSnapshot {
    for (auto index = 0uz; index < storage.generic_types.size(); ++index) {
        const auto type = GenericTypeID(program_identity, static_cast<std::uint32_t>(index));
        const auto& shape = storage.generic_types[index];
        if (const auto* application = std::get_if<GenericNominalApplication>(&shape)) {
            const auto source = generic_declaration_copy(application->definition);
            if ((std::holds_alternative<GenericRecordDefinition>(source)
                 && !storage.generic_record_shapes.contains(type))
                || (std::holds_alternative<GenericEnumDefinition>(source)
                    && !storage.generic_enum_shapes.contains(type))) {
                invariant_violation(
                    "generic shape was not prepared before sealing failure constraints"
                );
            }
        }
    }
    auto result = GenericAnalysisSnapshot {
        .owner = program_identity,
        .definitions = {},
        .types = storage.generic_types,
        .bodies = std::move(storage.generic_function_bodies),
        .records = std::move(storage.generic_record_shapes),
        .enum_payloads = std::move(storage.generic_enum_shapes),
        .test_stops = {},
        .failures = failures,
    };
    const auto normalize = [&](ConstructionTypeRef& type) noexcept {
        type = types.normalize(type);
    };
    for (auto index = 0uz; index < result.types.size(); ++index) {
        const auto type =
            types.normalize(GenericTypeID(program_identity, static_cast<std::uint32_t>(index)));
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            result.types[index] = *concrete;
        }
    }
    for (auto& type : result.types) {
        type.visit([&](auto& shape) noexcept {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, GenericArrayType>
                          || std::same_as<Shape, GenericSliceType>
                          || std::same_as<Shape, GenericVectorType>) {
                normalize(shape.element);
            } else if constexpr (std::same_as<Shape, GenericTupleType>) {
                for (auto& element : shape.elements) {
                    normalize(element);
                }
            } else if constexpr (std::same_as<Shape, GenericPointerType>) {
                normalize(shape.target);
            } else if constexpr (std::same_as<Shape, GenericOperationType>) {
                normalize(shape.success);
                shape.failures = failures->normalize(shape.failures);
            } else if constexpr (std::same_as<Shape, GenericCppNamedType>) {
                for (auto& argument : shape.arguments) {
                    normalize(argument);
                }
            } else if constexpr (std::same_as<Shape, GenericCppQueryType>) {
                shape = map_cpp_query_types(shape, [&](ConstructionTypeRef type) noexcept {
                    normalize(type);
                    return type;
                });
            } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
                for (auto& argument : shape.arguments) {
                    if (auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                        normalize(*type);
                    } else if (auto* effect = std::get_if<ConstructionFailureRef>(&argument)) {
                        *effect = failures->normalize(*effect);
                    }
                }
            }
        });
    }
    for (auto& [type, fields] : result.records) {
        for (auto& field : fields) {
            normalize(field);
        }
    }
    for (auto& [type, cases] : result.enum_payloads) {
        for (auto& payload : cases) {
            for (auto& element : payload) {
                normalize(element);
            }
        }
    }
    const auto normalize_body_type = [&](BodyType& type) noexcept {
        auto ref = type.construction();
        normalize(ref);
        type = BodyType(ref);
    };
    const auto normalize_failure = [&](BodyFailures& value) noexcept {
        value = BodyFailures(failures->normalize(value.reference()));
    };
    for (const auto& definition : storage.generic_definitions) {
        auto copy = *definition;
        if (auto* function = std::get_if<GenericFunctionDefinition>(&copy)) {
            for (auto& parameter : function->signature.parameters) {
                normalize(parameter.type);
            }
            normalize(function->signature.result);
            function->signature.failures = failures->normalize(function->signature.failures);
        }
        result.definitions.push_back(std::move(copy));
    }
    for (auto& [definition, body] : result.bodies) {
        auto bindings =
            std::move(body->bindings)
                .transform<ElaboratedLocalBinding>([&](LocalBindingID,
                                                       ElaboratedLocalBinding value) noexcept {
                    normalize(value.type);
                    return value;
                });
        auto patterns =
            std::move(body->patterns)
                .transform<ElaboratedPattern>([&](PatternID, ElaboratedPattern value) noexcept {
                    normalize(value.type);
                    if (auto* constraint =
                            std::get_if<ElaboratedTypeConstraintPattern>(&value.value)) {
                        normalize(constraint->type);
                    }
                    return value;
                });
        const auto visitor = Overloaded {
            [&](SemanticExpression& expression) noexcept {
                normalize_body_type(expression.type);
                normalize_failure(expression.failures);
                if (auto* native = std::get_if<SemCpp>(&expression.value)) {
                    if (auto* construction =
                            std::get_if<CppConstructOperation>(&native->operation)) {
                        normalize(construction->target);
                    }
                    if (auto* member = std::get_if<CppStaticMemberOperation>(&native->operation)) {
                        normalize(member->type);
                    }
                }
                if (auto* native = std::get_if<SemCppCall>(&expression.value)) {
                    if (auto* member = std::get_if<CppStaticMemberCallee<ConstructionTypeRef>>(
                            &native->callee
                        )) {
                        normalize(member->type);
                    }
                }
                if (auto* call = std::get_if<SemCall>(&expression.value)) {
                    normalize_failure(call->callee_failures);
                }
                if (auto* call = std::get_if<SemGenericCall>(&expression.value)) {
                    normalize_failure(call->callee_failures);
                    for (auto& argument : call->environment) {
                        if (auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                            normalize(*type);
                        } else if (auto* effect = std::get_if<ConstructionFailureRef>(&argument)) {
                            *effect = failures->normalize(*effect);
                        }
                    }
                }
                if (auto* compose = std::get_if<SemColdCompose>(&expression.value)) {
                    normalize_body_type(compose->success);
                }
                if (auto* attempt = std::get_if<SemTry>(&expression.value)) {
                    normalize_failure(attempt->protected_failures);
                    normalize_failure(attempt->residual_failures);
                    for (auto& arm : attempt->arms) {
                        normalize_failure(arm.accepted_failures);
                        for (auto& alternative : arm.alternatives) {
                            if (auto* typed =
                                    std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                                normalize_body_type(typed->type);
                            }
                        }
                    }
                }
            },
            [&](SemanticRegion& region) noexcept { normalize_failure(region.failures); },
        };
        visit_semantic_nodes(body->region, visitor);
        if (body->residual) {
            visit_semantic_nodes(*body->residual, visitor);
        }
        body = std::make_unique<StructuredBodyDraft>(StructuredBodyDraft {
            .id = body->id,
            .kind = body->kind,
            .provenance_identity = body->provenance_identity,
            .inputs = std::move(body->inputs),
            .lifetime_regions = std::move(body->lifetime_regions),
            .bindings = std::move(bindings).seal(),
            .patterns = std::move(patterns).seal(),
            .region = std::move(body->region),
            .residual = std::move(body->residual),
            .specialized = body->specialized,
            .generic_definition = body->generic_definition
        });
    }
    return result;
}
