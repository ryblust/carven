module carven:semantic.analysis.generics.closure.impl;

import :diagnostics.builder;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.traversal;
import :support.invariant;
import std;

auto ProgramDraft::normalize_generic_environment(
    std::span<const GenericArgument> arguments,
    const FailureSolution& failures
) noexcept -> std::optional<CanonicalGenericEnvironment> {
    const auto normalize_type = [&](auto&& self,
                                    ConstructionTypeRef type) noexcept -> std::optional<TypeID> {
        type = unwrap_type_alias(*this, type);
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            return *concrete;
        }
        if (const auto* term = std::get_if<TypeTermID>(&type)) {
            return ConstructionTypeStore::try_canonicalize_type(
                construction_type_copy(*term),
                storage.types,
                storage.callable_signatures,
                [&](ConstructionTypeRef child) noexcept { return self(self, child); },
                [&](ConstructionFailureRef effect) noexcept { return failures.closed_set(effect); }
            );
        }
        const auto shape = generic_type_copy(std::get<GenericTypeID>(type));
        if (const auto* operation = std::get_if<GenericOperationType>(&shape)) {
            const auto success = self(self, operation->success);
            const auto effect = failures.closed_set(operation->failures);
            if (success && effect) {
                return intern_type(
                    {.value = OperationTypeValue {.success = *success, .failures = *effect}}
                );
            }
        } else if (const auto* array = std::get_if<GenericArrayType>(&shape)) {
            if (const auto element = self(self, array->element)) {
                return intern_type(
                    {.value = ArrayTypeValue {.element = *element, .extent = array->extent}}
                );
            }
        } else if (const auto* slice = std::get_if<GenericSliceType>(&shape)) {
            if (const auto element = self(self, slice->element)) {
                return intern_type({.value = SliceTypeValue {.element = *element}});
            }
        } else if (const auto* sequence = std::get_if<GenericVectorType>(&shape)) {
            if (const auto element = self(self, sequence->element)) {
                return intern_type({.value = VectorTypeValue {.element = *element}});
            }
        } else if (const auto* pointer = std::get_if<GenericPointerType>(&shape)) {
            if (const auto target = self(self, pointer->target)) {
                return intern_type(
                    {.value = PointerTypeValue {.target = *target, .access = pointer->access}}
                );
            }
        } else if (const auto* tuple = std::get_if<GenericTupleType>(&shape)) {
            auto elements = std::vector<TypeID>();
            for (const auto element : tuple->elements) {
                const auto concrete = self(self, element);
                if (!concrete) {
                    return std::nullopt;
                }
                elements.push_back(*concrete);
            }
            return intern_type({.value = TupleTypeValue {.elements = std::move(elements)}});
        } else if (const auto* nominal = std::get_if<GenericNominalApplication>(&shape)) {
            const auto environment = normalize_generic_environment(nominal->arguments, failures);
            if (environment) {
                const auto source = generic_declaration_contract_copy(nominal->definition);
                const auto instance =
                    instantiate_generic_nominal(nominal->definition, *environment, source.origin);
                if (instance) {
                    return *instance;
                }
            }
        }
        return std::nullopt;
    };
    auto result = CanonicalGenericEnvironment();
    for (const auto& argument : arguments) {
        if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
            const auto closed = normalize_type(normalize_type, *type);
            if (!closed) {
                return std::nullopt;
            }
            result.emplace_back(*closed);
        } else if (const auto* effect = std::get_if<ConstructionFailureRef>(&argument)) {
            const auto closed = failures.closed_set(*effect);
            if (!closed) {
                return std::nullopt;
            }
            result.emplace_back(*closed);
        }
    }
    return result;
}

auto ProgramDraft::normalize_construction_types(const FailureSolution& failures) noexcept
    -> TypeResolution {
    auto result =
        std::move(storage.construction_types)
            .normalize(
                [&](const ConstructionType& type,
                    const auto& normalize_child) noexcept -> ConstructionTypeRef {
                    const auto concrete = ConstructionTypeStore::try_canonicalize_type(
                        type,
                        storage.types,
                        storage.callable_signatures,
                        [&](ConstructionTypeRef child) noexcept -> std::optional<TypeID> {
                            const auto normalized = normalize_child(child);
                            const auto* closed = std::get_if<TypeID>(&normalized);
                            return closed ? std::optional(*closed) : std::nullopt;
                        },
                        [&](ConstructionFailureRef effect) noexcept {
                            return failures.closed_set(effect);
                        }
                    );
                    if (concrete) {
                        return *concrete;
                    }
                    return type.value.visit([&](const auto& shape) noexcept -> ConstructionTypeRef {
                        using Shape = std::remove_cvref_t<decltype(shape)>;
                        if constexpr (std::same_as<Shape, ConstructionArrayTypeValue>) {
                            return intern_generic_type(
                                GenericArrayType {normalize_child(shape.element), shape.extent}
                            );
                        } else if constexpr (std::same_as<Shape, ConstructionSliceTypeValue>) {
                            return intern_generic_type(
                                GenericSliceType {normalize_child(shape.element)}
                            );
                        } else if constexpr (std::same_as<Shape, ConstructionTupleTypeValue>) {
                            auto elements = std::vector<ConstructionTypeRef>();
                            for (const auto child : shape.elements) {
                                elements.push_back(normalize_child(child));
                            }
                            return intern_generic_type(GenericTupleType {std::move(elements)});
                        } else if constexpr (std::same_as<Shape, ConstructionOperationTypeValue>) {
                            return intern_generic_type(
                                GenericOperationType {
                                    normalize_child(shape.success),
                                    failures.normalize(shape.failures)
                                }
                            );
                        } else {
                            static_assert(std::same_as<Shape, ConstructionCallableViewTypeValue>);
                            invariant_violation(
                                "symbolic callable view escaped its source admission boundary"
                            );
                        }
                    });
                }
            );
    auto normalized = std::map<GenericTypeID, ConstructionTypeRef>();
    const auto normalize = [&](auto&& self,
                               ConstructionTypeRef reference) noexcept -> ConstructionTypeRef {
        reference = result.normalize(reference);
        const auto* generic = std::get_if<GenericTypeID>(&reference);
        if (!generic) {
            return reference;
        }
        if (const auto found = normalized.find(*generic); found != normalized.end()) {
            return found->second;
        }
        const auto value = generic_type_copy(*generic);
        const auto child = [&](ConstructionTypeRef type) noexcept {
            return self(self, type);
        };
        const auto resolved = value.visit([&](const auto& shape) noexcept -> ConstructionTypeRef {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, TypeID>) {
                return shape;
            } else if constexpr (std::same_as<Shape, GenericArrayType>) {
                return make_array_type(*this, child(shape.element), shape.extent);
            } else if constexpr (std::same_as<Shape, GenericSliceType>) {
                return make_slice_type(*this, child(shape.element));
            } else if constexpr (std::same_as<Shape, GenericTupleType>) {
                auto elements = std::vector<ConstructionTypeRef>();
                for (const auto element : shape.elements) {
                    elements.push_back(child(element));
                }
                return make_tuple_type(*this, std::move(elements));
            } else if constexpr (std::same_as<Shape, GenericOperationType>) {
                const auto success = child(shape.success);
                const auto effect = failures.normalize(shape.failures);
                if (const auto* type = std::get_if<TypeID>(&success)) {
                    if (const auto* set = std::get_if<FailureSetID>(&effect)) {
                        return intern_type({.value = OperationTypeValue {*type, *set}});
                    }
                }
                // A recursive symbolic effect may intentionally retain its
                // Term. Final normalization cannot create new type terms after
                // consuming the construction store.
                return intern_generic_type(GenericOperationType {success, effect});
            } else if constexpr (std::same_as<Shape, GenericPointerType>) {
                const auto target = child(shape.target);
                if (const auto* concrete = std::get_if<TypeID>(&target)) {
                    return intern_type({.value = PointerTypeValue {*concrete, shape.access}});
                }
                return intern_generic_type(GenericPointerType {target, shape.access});
            } else if constexpr (std::same_as<Shape, GenericVectorType>) {
                const auto element = child(shape.element);
                if (const auto* concrete = std::get_if<TypeID>(&element)) {
                    return intern_type({.value = VectorTypeValue {*concrete}});
                }
                return intern_generic_type(GenericVectorType {element});
            } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
                auto environment = CanonicalGenericEnvironment();
                for (const auto& argument : shape.arguments) {
                    if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                        const auto value = child(*type);
                        const auto* concrete = std::get_if<TypeID>(&value);
                        if (!concrete) {
                            return *generic;
                        }
                        environment.emplace_back(*concrete);
                    } else if (const auto* failures_ref =
                                   std::get_if<ConstructionFailureRef>(&argument)) {
                        const auto effect = failures.closed_set(*failures_ref);
                        if (!effect) {
                            return *generic;
                        }
                        environment.emplace_back(*effect);
                    }
                }
                const auto found =
                    storage.generic_instance_index.find(std::pair(shape.definition, environment));
                if (found == storage.generic_instance_index.end()) {
                    return *generic;
                }
                return storage.generic_instances[found->second].instance.declaration.visit(
                    [&](auto nominal) noexcept -> ConstructionTypeRef {
                        if constexpr (std::same_as<decltype(nominal), StructID>) {
                            return intern_type({.value = StructTypeValue {nominal}});
                        } else {
                            return intern_type({.value = EnumTypeValue {nominal}});
                        }
                    }
                );
            } else {
                return *generic;
            }
        });
        normalized.emplace(*generic, resolved);
        return resolved;
    };
    auto generics = std::vector<ConstructionTypeRef>();
    for (auto index = 0uz; index < storage.generic_types.size(); ++index) {
        generics.push_back(
            normalize(normalize, GenericTypeID(program_identity, static_cast<std::uint32_t>(index)))
        );
    }
    return std::move(result).normalize_generics(std::move(generics));
}

auto ProgramDraft::close_generic_calls() noexcept -> AnalysisResult<void> {
    for (;;) {
        prepare_generic_analysis_shapes();
        const auto effects = complete_failure_applications({});
        if (!effects) {
            return std::unexpected(effects.error());
        }
        auto preview = solve_failure_constraints(
            storage.failure_constraints.snapshot(),
            storage.failure_sets,
            provenance_appender.reader(),
            FailureTypeDiagnosticNames(
                storage.types,
                storage.declarations.construction_view(),
                provenance_appender.reader()
            ),
            analysis_diagnostics,
            false
        );
        if (!preview) {
            return std::unexpected(preview.error());
        }
        auto progress = false;
        auto pending = std::optional<ProgramOriginID>();
        auto error = std::optional<AnalysisFailure>();
        const auto close = [&](SemanticExpression& expression) noexcept {
            auto* call = std::get_if<SemGenericCall>(&expression.value);
            if (!call || error) {
                return;
            }
            const auto environment = normalize_generic_environment(call->environment, *preview);
            if (!environment) {
                pending = expression.origin;
                return;
            }
            const auto bound = instantiate_generic_call(expression, *environment);
            if (!bound) {
                error = bound.error();
                return;
            }
            progress = true;
        };
        // Instance creation appends body slots. Their owned drafts stay stable.
        for (auto index = 0uz; index < storage.bodies.size(); ++index) {
            auto* body = storage.bodies[index].definition.get();
            if (!body) {
                continue;
            }
            visit_semantic_nodes(body->region, close);
            if (body->residual) {
                visit_semantic_nodes(*body->residual, close);
            }
        }
        if (error) {
            return std::unexpected(*error);
        }
        if (progress) {
            continue;
        }
        if (pending) {
            return std::unexpected(analysis_diagnostics.error(
                DiagnosticBuilder(
                    DiagnosticCode::TypeGenericArguments,
                    "generic call environment cannot be closed after its dependencies are complete"
                )
                    .primary(source_span(*pending))
                    .build()
            ));
        }
        return {};
    }
}

auto ProgramDraft::instantiate_generic_call(
    SemanticExpression& expression,
    std::span<const CanonicalGenericArgument> environment
) noexcept -> AnalysisResult<void> {
    auto* call = std::get_if<SemGenericCall>(&expression.value);
    if (!call) {
        invariant_violation("generic call closure received a different expression");
    }
    const auto target =
        instantiate_generic_function(call->definition, environment, expression.origin);
    if (!target) {
        return std::unexpected(target.error());
    }
    auto callee = SemanticExpression {
        .type = BodyType(intern_type({.value = FunctionTypeValue {*target}})),
        .lifetime = expression.lifetime,
        .origin = expression.origin,
        .constant = std::nullopt,
        .failures = BodyFailures(empty_failure_set()),
        .exits_test = false,
        .operation_reachable = expression.operation_reachable,
        .category = SemanticValueCategory::Value,
        .value = SemCallable {.callable = *target},
    };
    const auto contract = construction_callable_contract_copy(*target);
    auto arguments = std::move(call->arguments);
    if (contract.execution == CallableExecutionKind::Async) {
        expression.value =
            SemColdCall {OwnedSemanticExpression(std::move(callee)), *target, std::move(arguments)};
    } else {
        expression.value = SemCall {
            OwnedSemanticExpression(std::move(callee)),
            *target,
            std::move(arguments),
            BodyFailures(contract.failures)
        };
    }
    return {};
}
