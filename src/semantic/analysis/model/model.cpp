module carven:semantic.analysis.model.impl;

import :semantic.analysis.model;
import :support.invariant;
import std;

AnalysisFailureKey::AnalysisFailureKey(TypeID type) noexcept
    : value(type) {}

AnalysisFailureKey::AnalysisFailureKey(AnalysisSymbolicFailure failure) noexcept
    : value(std::move(failure)) {}

AnalysisBodyView::AnalysisBodyView(const SemIRBody& body) noexcept
    : source(&body) {
    for (const auto [id, binding] : body.bindings()) {
        binding_rows.emplace_back(
            id,
            ElaboratedLocalBinding {
                .name = binding.name,
                .type = binding.type,
                .lifetime = binding.lifetime,
                .storage = binding.storage,
                .origin = binding.origin
            }
        );
    }
    for (const auto [id, pattern] : body.patterns()) {
        static_cast<void>(id);
        auto value =
            pattern.value.visit([](const auto& value) static noexcept -> ElaboratedPatternValue {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, TypeConstraintPattern>) {
                    return ElaboratedTypeConstraintPattern {.type = value.type};
                } else {
                    return value;
                }
            });
        pattern_rows.push_back({pattern.type, std::move(value), pattern.origin});
    }
}

AnalysisBodyView::AnalysisBodyView(const StructuredBodyDraft& body) noexcept
    : source(&body) {
    for (const auto [id, binding] : body.bindings.entries()) {
        binding_rows.emplace_back(id, binding);
    }
    for (const auto [id, pattern] : body.patterns.entries()) {
        static_cast<void>(id);
        pattern_rows.push_back(pattern);
    }
}

auto AnalysisBodyView::id() const noexcept -> AnalysisBodyID {
    return source.visit([](const auto* body) static noexcept -> AnalysisBodyID {
        using Body = std::remove_cvref_t<decltype(*body)>;
        if constexpr (std::same_as<Body, SemIRBody>) {
            return body->id();
        } else {
            if (!body->generic_definition) {
                invariant_violation("generic analysis body lacks definition identity");
            }
            return *body->generic_definition;
        }
    });
}

auto AnalysisBodyView::inputs() const noexcept -> const BodyInputs& {
    return source.visit([](const auto* body) static noexcept -> const BodyInputs& {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(*body)>, SemIRBody>) {
            return body->inputs();
        } else {
            return body->inputs;
        }
    });
}

auto AnalysisBodyView::lifetime_regions() const noexcept -> const LifetimeRegionTree& {
    return source.visit([](const auto* body) static noexcept -> const LifetimeRegionTree& {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(*body)>, SemIRBody>) {
            return body->lifetime_regions();
        } else {
            return body->lifetime_regions;
        }
    });
}

auto AnalysisBodyView::bindings() const noexcept
    -> std::span<const std::pair<LocalBindingID, ElaboratedLocalBinding>> {
    return binding_rows;
}

auto AnalysisBodyView::binding(LocalBindingID id) const noexcept -> const ElaboratedLocalBinding& {
    const auto& row = binding_rows.at(id.index());
    if (row.first != id) {
        invariant_violation("analysis binding belongs to a foreign body");
    }
    return row.second;
}

auto AnalysisBodyView::pattern(PatternID id) const noexcept -> const ElaboratedPattern& {
    if (id.owner() != lifetime_regions().owner()) {
        invariant_violation("analysis pattern belongs to a foreign body");
    }
    return pattern_rows.at(id.index());
}

auto AnalysisBodyView::elaborated_pattern_table() const noexcept
    -> const ImmutableBodyTable<ElaboratedPattern, PatternID>& {
    const auto* body = std::get_if<const StructuredBodyDraft*>(&source);
    if (!body) {
        invariant_violation("elaborated pattern table requires a definition body");
    }
    return (*body)->patterns;
}

auto AnalysisBodyView::pattern_table() const noexcept
    -> const ImmutableBodyTable<Pattern, PatternID>& {
    const auto* body = concrete();
    if (!body) {
        invariant_violation("rigid coverage requires definition-site structural coverage");
    }
    return body->pattern_table();
}

auto AnalysisBodyView::region() const noexcept -> const SemanticRegion& {
    return source.visit([](const auto* body) static noexcept -> const SemanticRegion& {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(*body)>, SemIRBody>) {
            return body->region();
        } else {
            return body->region;
        }
    });
}

auto AnalysisBodyView::specialized() const noexcept -> std::optional<BodyID> {
    return source.visit([](const auto* body) static noexcept -> std::optional<BodyID> {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(*body)>, SemIRBody>) {
            return body->specialized();
        } else {
            return std::nullopt;
        }
    });
}

auto AnalysisBodyView::concrete() const noexcept -> const SemIRBody* {
    const auto* body = std::get_if<const SemIRBody*>(&source);
    return body ? *body : nullptr;
}

AnalysisModelView::AnalysisModelView(
    const SemIRProgram& program,
    const GenericAnalysisSnapshot& generics
) noexcept
    : program(program),
      generics(generics) {
    for (const auto [id, body] : program.bodies().entries()) {
        body_views.emplace(id, AnalysisBodyView(body));
    }
    for (const auto& [id, body] : generics.bodies) {
        body_views.emplace(id, AnalysisBodyView(*body));
    }
}

auto AnalysisModelView::bodies() const noexcept
    -> const std::map<AnalysisBodyID, AnalysisBodyView>& {
    return body_views;
}

auto AnalysisModelView::body(AnalysisBodyID id) const noexcept -> const AnalysisBodyView& {
    return body_views.at(id);
}

auto AnalysisModelView::shape(ConstructionTypeRef type) const noexcept -> const AnalysisTypeShape& {
    const auto found = shapes.find(type);
    if (found != shapes.end()) {
        return found->second;
    }
    return shapes.emplace(type, compute_shape(type)).first->second;
}

auto AnalysisModelView::compute_shape(ConstructionTypeRef type) const noexcept
    -> AnalysisTypeShape {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        return program.types().type(*concrete).value.visit(
            [&](const auto& shape) noexcept -> AnalysisTypeShape {
                using Shape = std::remove_cvref_t<decltype(shape)>;
                if constexpr (std::same_as<Shape, ArrayTypeValue>) {
                    return ConstructionArrayTypeValue {
                        .element = shape.element,
                        .extent = shape.extent
                    };
                } else if constexpr (std::same_as<Shape, TupleTypeValue>) {
                    auto elements = std::vector<ConstructionTypeRef>();
                    for (const auto element : shape.elements) {
                        elements.push_back(element);
                    }
                    return ConstructionTupleTypeValue {.elements = std::move(elements)};
                } else if constexpr (std::same_as<Shape, SliceTypeValue>) {
                    return ConstructionSliceTypeValue {.element = shape.element};
                } else if constexpr (std::same_as<Shape, VectorTypeValue>) {
                    return GenericVectorType {.element = shape.element};
                } else if constexpr (std::same_as<Shape, PointerTypeValue>) {
                    return AnalysisPointerType {.target = shape.target, .access = shape.access};
                } else if constexpr (std::same_as<Shape, CallableViewTypeValue>) {
                    const auto& signature =
                        program.callable_signatures().signature(shape.signature);
                    auto parameters = std::vector<ConstructionCallableParameter>();
                    for (const auto& parameter : signature.parameters) {
                        parameters.push_back(
                            {.stage = parameter.stage,
                             .access = parameter.access,
                             .type = parameter.type}
                        );
                    }
                    return ConstructionCallableViewTypeValue {
                        .parameters = std::move(parameters),
                        .result = signature.result,
                        .failures = signature.failures
                    };
                } else if constexpr (std::same_as<Shape, OperationTypeValue>) {
                    return ConstructionOperationTypeValue {
                        .success = shape.success,
                        .failures = shape.failures
                    };
                } else {
                    return shape;
                }
            }
        );
    }
    const auto* generic = std::get_if<GenericTypeID>(&type);
    if (!generic) {
        invariant_violation("unsolved construction term reached analysis model");
    }
    return generics.generic_type(*generic).visit(
        [&](const auto& shape) noexcept -> AnalysisTypeShape {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, TypeID>) {
                return this->shape(shape);
            } else if constexpr (std::same_as<Shape, GenericArrayType>) {
                return ConstructionArrayTypeValue {
                    .element = shape.element,
                    .extent = shape.extent
                };
            } else if constexpr (std::same_as<Shape, GenericTupleType>) {
                auto elements = std::vector<ConstructionTypeRef>();
                for (const auto element : shape.elements) {
                    elements.push_back(element);
                }
                return ConstructionTupleTypeValue {.elements = std::move(elements)};
            } else if constexpr (std::same_as<Shape, GenericSliceType>) {
                return ConstructionSliceTypeValue {.element = shape.element};
            } else if constexpr (std::same_as<Shape, GenericPointerType>) {
                return AnalysisPointerType {.target = shape.target, .access = shape.access};
            } else if constexpr (std::same_as<Shape, GenericCallableViewType>) {
                return ConstructionCallableViewTypeValue {
                    .parameters = shape.parameters,
                    .result = shape.result,
                    .failures = shape.failures
                };
            } else if constexpr (std::same_as<Shape, GenericOperationType>) {
                return ConstructionOperationTypeValue {
                    .success = shape.success,
                    .failures = shape.failures
                };
            } else {
                return shape;
            }
        }
    );
}

auto AnalysisModelView::enum_case_count(ConstructionEnumCaseRef reference) const noexcept
    -> std::size_t {
    if (const auto* concrete = std::get_if<EnumCaseID>(&reference)) {
        const auto owner = program.declarations().enum_case(*concrete).owner;
        return program.declarations().enumeration(owner).cases.size();
    }
    const auto& shape = generics.generic_type(std::get<GenericEnumCaseRef>(reference).owner);
    const auto definition = std::get<GenericNominalApplication>(shape).definition;
    return std::get<GenericEnumDefinition>(generics.definition(definition)).cases.size();
}

auto AnalysisModelView::concrete_type(ConstructionTypeRef type) const noexcept
    -> std::optional<TypeID> {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        return *concrete;
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        if (const auto* concrete = std::get_if<TypeID>(&generics.generic_type(*generic))) {
            return *concrete;
        }
    }
    return std::nullopt;
}

auto AnalysisModelView::callable_identity(ConstructionTypeRef type) const noexcept
    -> std::optional<ConstructionCallableRef> {
    if (std::holds_alternative<GenericClosureType>(shape(type))) {
        return std::get<GenericTypeID>(type);
    }
    const auto concrete = concrete_type(type);
    if (concrete) {
        if (const auto callable = ::callable_identity(program, *concrete)) {
            return *callable;
        }
    }
    return std::nullopt;
}

auto AnalysisModelView::callable_body(ConstructionCallableRef callable) const noexcept
    -> std::optional<AnalysisBodyID> {
    if (const auto* generic = std::get_if<GenericTypeID>(&callable)) {
        return std::get<GenericClosureType>(generics.generic_type(*generic)).definition;
    }
    const auto body = program.declarations().body_for_callable(std::get<CallableID>(callable));
    return body ? std::optional<AnalysisBodyID>(*body) : std::nullopt;
}

auto AnalysisModelView::callable_contract(ConstructionCallableRef callable) const noexcept
    -> ConstructionCallableContract {
    if (const auto* generic = std::get_if<GenericTypeID>(&callable)) {
        const auto& closure = std::get<GenericClosureType>(generics.generic_type(*generic));
        return std::get<GenericCallableDefinition>(generics.definition(closure.definition))
            .signature;
    }
    const auto& signature = program.callable_signatures().signature(
        program.declarations().callable(std::get<CallableID>(callable)).signature
    );
    auto parameters = std::vector<ConstructionCallableParameter>();
    for (const auto& parameter : signature.parameters) {
        parameters.push_back({
            .stage = parameter.stage,
            .access = parameter.access,
            .type = parameter.type,
        });
    }
    return {
        .execution = signature.execution,
        .parameters = std::move(parameters),
        .result = signature.result,
        .failures = signature.failures,
        .policy = FailureContractPolicy::Declared,
    };
}

auto AnalysisModelView::callable_adaptation(
    ConstructionTypeRef from,
    ConstructionTypeRef to
) const noexcept -> CallableAdaptation {
    const auto source_type = concrete_type(from);
    const auto target_type = concrete_type(to);
    if (source_type && target_type) {
        return ::callable_adaptation(program, *source_type, *target_type);
    }
    const auto* target = std::get_if<ConstructionCallableViewTypeValue>(&shape(to));
    if (from == to || !target) {
        return {.kind = CallableAdaptationKind::CopyTarget, .callable = std::nullopt};
    }
    if (const auto* source = std::get_if<ConstructionCallableViewTypeValue>(&shape(from))) {
        const auto source_failures = failure_closed_set(source->failures);
        const auto target_failures = failure_closed_set(target->failures);
        const auto copy = source->failures == target->failures
            || (source_failures && target_failures && *source_failures == *target_failures)
            || (target_failures
                && program.failure_sets().failure_set(*target_failures).members.empty());
        return {
            .kind =
                copy ? CallableAdaptationKind::CopyTarget : CallableAdaptationKind::BorrowObject,
            .callable = std::nullopt
        };
    }
    const auto callable = callable_identity(from);
    if (!callable) {
        invariant_violation("callable adaptation requires a callable source");
    }
    if (std::holds_alternative<FunctionTypeValue>(shape(from))) {
        return {.kind = CallableAdaptationKind::FunctionTarget, .callable = callable};
    }
    const auto body = callable_body(*callable);
    if (!body) {
        invariant_violation("closure adaptation requires a completed body");
    }
    return {
        .kind = this->body(*body).inputs().captures.empty()
            ? CallableAdaptationKind::StatelessClosure
            : CallableAdaptationKind::BorrowObject,
        .callable = callable
    };
}

auto AnalysisModelView::contents(ConstructionTypeRef type) const noexcept -> TypeContents {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        return program.type_contents(*concrete);
    }
    if (const auto found = content_facts.find(type); found != content_facts.end()) {
        return found->second;
    }
    const auto facts = collect_type_contents(
        std::array {type},
        [&](ConstructionTypeRef current, auto depend) noexcept -> unsigned {
            if (const auto concrete = concrete_type(current)) {
                return program.type_contents(*concrete).mask();
            }
            return shape(current).visit([&](const auto& value) noexcept {
                const auto members = [&](const auto&) noexcept {
                    return fields(std::get<GenericTypeID>(current));
                };
                return type_content_mask(value, members, depend);
            });
        }
    );
    for (const auto& [reference, contents] : facts) {
        content_facts.emplace(reference, contents);
    }
    return facts.front().second;
}

auto AnalysisModelView::fields(GenericTypeID type) const noexcept
    -> std::vector<ConstructionTypeRef> {
    if (const auto record = generics.records.find(type); record != generics.records.end()) {
        return record->second;
    }
    auto result = std::vector<ConstructionTypeRef>();
    if (const auto enumeration = generics.enum_payloads.find(type);
        enumeration != generics.enum_payloads.end()) {
        for (const auto& payload : enumeration->second) {
            result.append_range(payload);
        }
        return result;
    }
    invariant_violation("generic analysis nominal shape lacks checked fields");
}

auto AnalysisModelView::failure_closed_set(ConstructionFailureRef failures) const noexcept
    -> std::optional<FailureSetID> {
    if (const auto* set = std::get_if<FailureSetID>(&failures)) {
        return *set;
    }
    if (!generics.failures) {
        invariant_violation("symbolic failure query lacks its shared solution");
    }
    return generics.failures->closed_set(failures);
}

auto AnalysisModelView::failure_support(ConstructionFailureRef failures) const noexcept
    -> GenericFailureMask {
    if (const auto set = failure_closed_set(failures)) {
        return {.complemented = false, .members = program.failure_sets().failure_set(*set).members};
    }
    return generics.failures->support(failures);
}

auto AnalysisModelView::failure_known_members(ConstructionFailureRef failures) const noexcept
    -> std::vector<TypeID> {
    if (const auto set = failure_closed_set(failures)) {
        return program.failure_sets().failure_set(*set).members;
    }
    return generics.failures->known_members(failures);
}

auto AnalysisModelView::failure_may_contain(
    ConstructionFailureRef failures,
    TypeID member
) const noexcept -> bool {
    const auto mask = failure_support(failures);
    return std::ranges::contains(mask.members, member) != mask.complemented;
}
