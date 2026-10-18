module carven:semantic.analysis.generics.functions.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.generic;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

auto ProgramDraft::substitute_function_type(
    ConstructionTypeRef type,
    GenericDeclarationID definition,
    std::span<const CanonicalGenericArgument> arguments,
    ProgramOriginID origin,
    FailureInstantiation* effects
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    if (!effects) {
        auto session = make_failure_instantiation(definition, generic_environment(arguments));
        return substitute_function_type(type, definition, arguments, origin, &session);
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto shape = generic_type_copy(*generic);
        const auto child = [&](ConstructionTypeRef type) noexcept {
            return substitute_function_type(type, definition, arguments, origin, effects);
        };
        if (const auto* array = std::get_if<GenericArrayType>(&shape)) {
            auto element = child(array->element);
            if (!element) {
                return std::unexpected(element.error());
            }
            return make_array_type(*this, *element, array->extent);
        }
        if (const auto* tuple = std::get_if<GenericTupleType>(&shape)) {
            auto elements = std::vector<ConstructionTypeRef>();
            for (const auto element : tuple->elements) {
                auto resolved = child(element);
                if (!resolved) {
                    return std::unexpected(resolved.error());
                }
                elements.push_back(*resolved);
            }
            return make_tuple_type(*this, std::move(elements));
        }
        if (const auto* slice = std::get_if<GenericSliceType>(&shape)) {
            auto element = child(slice->element);
            if (!element) {
                return std::unexpected(element.error());
            }
            return make_slice_type(*this, *element);
        }
        if (const auto* operation = std::get_if<GenericOperationType>(&shape)) {
            auto success = child(operation->success);
            if (!success) {
                return std::unexpected(success.error());
            }
            return make_operation_type(*this, *success, effects->substitute(operation->failures));
        }
        auto resolved = resolve_generic_type(*generic, definition, arguments, origin, effects);
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        return ConstructionTypeRef {*resolved};
    }
    const auto* term = std::get_if<TypeTermID>(&type);
    if (!term) {
        return type;
    }
    auto source = construction_type_copy(*term);
    auto error = std::optional<AnalysisFailure>();
    const auto substitute = [&](ConstructionTypeRef& field) noexcept {
        if (error) {
            return;
        }
        auto changed = substitute_function_type(field, definition, arguments, origin, effects);
        if (!changed) {
            error = changed.error();
        } else {
            field = *changed;
        }
    };
    source.value.visit([&](auto& shape) noexcept {
        using T = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<T, ConstructionTupleTypeValue>) {
            for (auto& element : shape.elements) {
                substitute(element);
            }
        } else if constexpr (std::same_as<T, ConstructionCallableViewTypeValue>) {
            for (auto& parameter : shape.parameters) {
                substitute(parameter.type);
            }
            substitute(shape.result);
            shape.failures = effects->substitute(shape.failures);
        } else if constexpr (std::same_as<T, ConstructionOperationTypeValue>) {
            substitute(shape.success);
            shape.failures = effects->substitute(shape.failures);
        } else {
            substitute(shape.element);
        }
    });
    if (error) {
        return std::unexpected(*error);
    }
    return append_construction_type(std::move(source));
}

auto ProgramDraft::instantiate_generic_function(
    GenericDeclarationID definition,
    std::span<const CanonicalGenericArgument> arguments,
    ProgramOriginID origin
) noexcept -> AnalysisResult<CallableID> {
    const auto declaration = generic_declaration_copy(definition);
    const auto* function = std::get_if<GenericFunctionDefinition>(&declaration);
    if (!function || !has_generic_function_body(definition)) {
        return std::unexpected(
            diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::TypeGenericDefinition,
                    "generic function instantiation requires its checked definition body"
                )
                    .primary(source_span(origin))
                    .build()
            )
        );
    }
    const auto key =
        std::pair(definition, CanonicalGenericEnvironment(arguments.begin(), arguments.end()));
    if (const auto found = storage.generic_function_instance_index.find(key);
        found != storage.generic_function_instance_index.end()) {
        const auto& slot = storage.generic_function_instances[found->second];
        if (const auto* failure = std::get_if<AnalysisFailure>(&slot.state)) {
            return std::unexpected(*failure);
        }
        return slot.callable;
    }
    if (arguments.size() != function->contract.parameters.size()) {
        return std::unexpected(
            diagnostics().error(DiagnosticBuilder(
                                    DiagnosticCode::TypeGenericArguments,
                                    "generic function argument count differs from its declaration"
            )
                                    .primary(source_span(origin))
                                    .build())
        );
    }
    for (const auto [index, argument] : std::views::enumerate(arguments)) {
        if (!generic_argument_matches(function->contract.parameter_kinds[index], argument)) {
            return std::unexpected(
                diagnostics().error(DiagnosticBuilder(
                                        DiagnosticCode::TypeGenericArguments,
                                        "generic argument kind differs from its parameter"
                )
                                        .primary(source_span(origin))
                                        .build())
            );
        }
        if (const auto* effect = std::get_if<FailureSetID>(&argument)) {
            for (const auto member : failure_set_copy(*effect).members) {
                auto admitted = require_failure_member(member, origin);
                if (!admitted) {
                    return std::unexpected(admitted.error());
                }
            }
        }
    }
    auto formation_arguments = generic_environment(arguments);
    auto formed = check_generic_type_formations(definition, formation_arguments, origin);
    if (!formed) {
        return std::unexpected(formed.error());
    }
    if (*formed != TypeFormationClosure::Complete) {
        return std::unexpected(
            diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::TypeGenericDefinition,
                    "generic instantiation requires completed type formation contracts"
                )
                    .primary(source_span(origin))
                    .build()
            )
        );
    }
    if (storage.generic_function_instances.size() >= maximum_generic_instances
        || storage.generic_instance_depth >= maximum_generic_depth) {
        return std::unexpected(
            diagnostics().error(DiagnosticBuilder(
                                    DiagnosticCode::TypeGenericLimits,
                                    "generic function construction exceeded its instance budget"
            )
                                    .primary(source_span(origin))
                                    .build())
        );
    }
    ++storage.generic_instance_depth;

    struct Depth final {
        std::size_t& value;

        ~Depth() { --value; }
    };

    const auto depth = Depth {storage.generic_instance_depth};
    auto effects = make_failure_instantiation(definition, formation_arguments);
    auto contract = function->signature;
    contract.failures = effects.substitute(contract.failures);
    for (auto& parameter : contract.parameters) {
        auto type =
            substitute_function_type(parameter.type, definition, key.second, origin, &effects);
        if (!type) {
            return std::unexpected(type.error());
        }
        parameter.type = *type;
    }
    auto result =
        substitute_function_type(contract.result, definition, key.second, origin, &effects);
    if (!result) {
        return std::unexpected(result.error());
    }
    contract.result = *result;
    const auto callable = storage.declarations.append_body_callable(std::move(contract));
    const auto function_id = storage.declarations.append_function_instance({
        .module_id = function->contract.module_id,
        .name = function->contract.name,
        .origin = function->contract.origin,
        .visibility = DeclarationVisibility::Module,
        .callable = callable,
        .entry_point = std::nullopt,
        .cpp_export_origin = std::nullopt,
        .is_const = function->is_const,
    });
    storage.functions_by_callable.emplace(callable, function_id);
    const auto reservation = reserve_body(BodyKind::Function);
    const auto body_id = reservation.id();
    complete_callable(callable, FunctionBodyImplementation {.body = body_id});
    const auto slot_index = storage.generic_function_instances.size();
    storage.generic_function_instance_index.emplace(key, slot_index);
    storage.generic_function_instances.push_back({
        .definition = definition,
        .arguments = key.second,
        .callable = callable,
        .state = ConstructionStorage::GenericFunctionInstanceSlot::Progress::Instantiating,
    });
    const auto& source = generic_function_body(definition);
    auto error = std::optional<AnalysisFailure>();
    const auto substitute = [&](ConstructionTypeRef& type) noexcept {
        if (error) {
            return;
        }
        auto resolved = substitute_function_type(type, definition, key.second, origin, &effects);
        if (!resolved) {
            error = resolved.error();
        } else {
            type = *resolved;
        }
    };
    const auto substitute_body_type = [&](BodyType& type) noexcept {
        auto reference = type.construction();
        substitute(reference);
        type = BodyType(reference);
    };
    const auto substitute_owner = [&](ConstructionStructRef& owner) noexcept {
        const auto* generic = std::get_if<GenericTypeID>(&owner);
        if (!generic || error) {
            return;
        }
        auto resolved = resolve_generic_type(*generic, definition, key.second, origin, &effects);
        if (!resolved) {
            error = resolved.error();
            return;
        }
        const auto concrete = type_copy(*resolved);
        const auto* record = std::get_if<StructTypeValue>(&concrete.value);
        if (!record) {
            invariant_violation("generic record projection resolved to a non-record");
        }
        owner = record->structure;
    };
    auto bindings = source.bindings.clone().transform<ElaboratedLocalBinding>(
        [&](LocalBindingID, ElaboratedLocalBinding binding) noexcept {
            substitute(binding.type);
            return binding;
        }
    );
    auto patterns = source.patterns.clone().transform<ElaboratedPattern>(
        [&](PatternID, ElaboratedPattern pattern) noexcept {
            substitute(pattern.type);
            if (auto* constraint = std::get_if<ElaboratedTypeConstraintPattern>(&pattern.value)) {
                substitute(constraint->type);
            }
            return pattern;
        }
    );
    auto region = source.region;
    auto residual = source.residual;
    const auto visitor = Overloaded {
        [&](SemanticExpression& expression) noexcept {
            substitute_body_type(expression.type);
            expression.failures = BodyFailures(effects.substitute(expression.failures.reference()));
            if (auto* call = std::get_if<SemCall>(&expression.value)) {
                call->callee_failures =
                    BodyFailures(effects.substitute(call->callee_failures.reference()));
            }
            if (auto* call = std::get_if<SemGenericCall>(&expression.value)) {
                call->callee_failures =
                    BodyFailures(effects.substitute(call->callee_failures.reference()));
            }
            if (auto* native = std::get_if<SemCpp>(&expression.value)) {
                if (auto* construction = std::get_if<CppConstructOperation>(&native->operation)) {
                    substitute(construction->target);
                }
                if (auto* member = std::get_if<CppStaticMemberOperation>(&native->operation)) {
                    substitute(member->type);
                }
            }
            if (auto* native = std::get_if<SemCppCall>(&expression.value)) {
                if (auto* member =
                        std::get_if<CppStaticMemberCallee<ConstructionTypeRef>>(&native->callee)) {
                    substitute(member->type);
                }
            }
            if (auto* record = std::get_if<SemStruct>(&expression.value)) {
                substitute_owner(record->structure);
            }
            if (auto* field = std::get_if<SemField>(&expression.value)) {
                substitute_owner(field->field.owner);
            }
            if (auto* compose = std::get_if<SemColdCompose>(&expression.value)) {
                substitute_body_type(compose->success);
            }
            if (auto* attempt = std::get_if<SemTry>(&expression.value)) {
                attempt->protected_failures =
                    BodyFailures(effects.substitute(attempt->protected_failures.reference()));
                attempt->residual_failures =
                    BodyFailures(effects.substitute(attempt->residual_failures.reference()));
                for (auto& arm : attempt->arms) {
                    arm.accepted_failures =
                        BodyFailures(effects.substitute(arm.accepted_failures.reference()));
                    for (auto& alternative : arm.alternatives) {
                        if (auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                            substitute_body_type(typed->type);
                        }
                    }
                }
            }
        },
        [&](SemanticRegion& region) noexcept {
            region.failures = BodyFailures(effects.substitute(region.failures.reference()));
        },
    };
    visit_semantic_nodes(region, visitor);
    if (residual) {
        visit_semantic_nodes(*residual, visitor);
    }
    // Resolve already bound generic targets; recursive keys observe the reservation above.
    const auto calls = [&](SemanticExpression& expression) noexcept {
        auto* call = std::get_if<SemGenericCall>(&expression.value);
        if (!call || error) {
            return;
        }
        auto environment = GenericEnvironment();
        auto types = CanonicalGenericEnvironment();
        auto closed = true;
        for (const auto& argument : call->environment) {
            if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                auto result = substitute_function_type(
                    *type,
                    definition,
                    key.second,
                    expression.origin,
                    &effects
                );
                if (!result) {
                    error = result.error();
                    return;
                }
                const auto substituted = unwrap_type_alias(*this, *result);
                environment.emplace_back(substituted);
                if (const auto* value = std::get_if<TypeID>(&substituted)) {
                    types.emplace_back(*value);
                } else {
                    closed = false;
                }
            } else if (const auto* effect = std::get_if<ConstructionFailureRef>(&argument)) {
                const auto substituted = effects.substitute(*effect);
                environment.emplace_back(substituted);
                if (const auto* value = std::get_if<FailureSetID>(&substituted)) {
                    types.emplace_back(*value);
                } else {
                    closed = false;
                }
            }
        }
        call->environment = std::move(environment);
        if (!closed) {
            return;
        }
        auto instantiated = instantiate_generic_call(expression, types);
        if (!instantiated) {
            error = instantiated.error();
        }
    };
    visit_semantic_nodes(region, calls);
    if (residual) {
        visit_semantic_nodes(*residual, calls);
    }
    if (error) {
        storage.generic_function_instances[slot_index].state = *error;
        return std::unexpected(*error);
    }
    add_body_draft({
        .id = body_id,
        .kind = source.kind,
        .provenance_identity = source.provenance_identity,
        .inputs = source.inputs,
        .lifetime_regions = source.lifetime_regions.clone(),
        .bindings = std::move(bindings).seal(),
        .patterns = std::move(patterns).seal(),
        .region = std::move(region),
        .residual = std::move(residual),
        .specialized = std::nullopt,
        .generic_definition = definition,
    });
    storage.generic_function_instances[slot_index].state =
        ConstructionStorage::GenericFunctionInstanceSlot::Progress::Complete;
    return callable;
}
