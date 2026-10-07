module carven:semantic.analysis.body.builder.impl;

import :semantic.analysis.body.builder;
import :semantic.analysis.constant.fold;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.children;
import :semantic.semir.completion;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

BodyBuilder::BodyBuilder(BodyReservation reservation, ProgramDraft& draft) noexcept
    : draft(draft),
      body_identity(reservation.body_id.owner(), reservation.body_id.index()),
      body_id(reservation.body_id),
      body_kind(reservation.body_kind),
      provenance_identity(reservation.provenance_identity),
      lifetime_regions(body_identity),
      bindings(body_identity),
      patterns(body_identity) {}

auto BodyBuilder::completion_patterns() const noexcept -> CompletionPatterns {
    return {
        .read =
            [this](PatternID id) noexcept -> std::variant<PatternValue, ElaboratedPatternValue> {
            return pattern_copy(id).value;
        },
        .single_case =
            [this](EnumCaseID id) noexcept {
                const auto owner = draft.construction_enum_case_declaration_copy(id).owner;
                return draft.enum_cases(owner).size() == 1uz;
            },
    };
}

auto BodyBuilder::make_expression(
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    ProgramOriginID origin,
    SemanticExpressionValue&& value,
    std::optional<ConstantID> constant
) noexcept -> SemanticExpression {
    if (const auto* known = std::get_if<SemConstant>(&value)) {
        constant = known->constant;
    }
    // Aggregate values are static only when every initializer is static.
    if (!constant) {
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            if (const auto* array = std::get_if<SemArray>(&value)) {
                constant = array_constant(draft, *concrete, *array);
            } else if (const auto* structure = std::get_if<SemStruct>(&value)) {
                constant = struct_constant(draft, *concrete, *structure);
            }
        }
    }
    auto failures = draft.add_empty_failure_term();
    const auto add = [&](const SemanticExpression& child) noexcept {
        draft.add_failure_contribution(failures, child.failures.term());
    };
    const auto add_region = [&](const SemanticRegion& region) noexcept {
        draft.add_failure_contribution(failures, region.failures.term());
        if (region.result.has_value()) {
            add(*region.result);
        }
    };
    const auto add_selected = [&](const auto& node) noexcept {
        visit_evaluation_children(node, [&](const auto& child) noexcept {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(child)>, SemanticRegion>) {
                add_region(child);
            } else {
                add(child);
            }
        });
    };
    value.visit(
        Overloaded {
            [&](const SemShortCircuit& node) noexcept { add_selected(node); },
            [&](const SemIf& node) noexcept { add_selected(node); },
            [&](const SemMatch& node) noexcept { add_selected(node); },
            [&]<typename Operation>(const Operation& node) noexcept
                requires std::same_as<Operation, SemDefault>
                             || std::same_as<Operation, SemConstant>
                             || std::same_as<Operation, SemUnreachable>
                             || std::same_as<Operation, SemBinding>
                             || std::same_as<Operation, SemCallable>
                             || std::same_as<Operation, SemEnumConstructor>
                             || std::same_as<Operation, SemCpp>
                             || std::same_as<Operation, SemCppCall>
                             || std::same_as<Operation, SemRange>
                             || std::same_as<Operation, SemArray>
                             || std::same_as<Operation, SemArrayAdopt>
                             || std::same_as<Operation, SemStruct>
                             || std::same_as<Operation, SemEnumCase>
                             || std::same_as<Operation, SemUnary>
                             || std::same_as<Operation, SemBinary>
                             || std::same_as<Operation, SemCast>
                             || std::same_as<Operation, SemDereference>
                             || std::same_as<Operation, SemAddressOf>
                             || std::same_as<Operation, SemField>
                             || std::same_as<Operation, SemIndex>
                             || std::same_as<Operation, SemPrint>
                             || std::same_as<Operation, SemFormat>
                             || std::same_as<Operation, SemIntrinsic>
                             || std::same_as<Operation, SemClosure>
                             || std::same_as<Operation, SemBorrowCallable>
                             || std::same_as<Operation, SemTake>
                             || std::same_as<Operation, SemPropagate>
                             || std::same_as<Operation, SemColdCall>
                             || std::same_as<Operation, SemAwait>
                             || std::same_as<Operation, SemAsyncIntrinsic>
            { visit_semantic_children(node, add); },
            [&](const SemCall& node) noexcept {
                visit_semantic_children(node, add);
                draft.add_failure_contribution(failures, node.callee_failures.term());
            },
            [&](const SemReport& node) noexcept { add_selected(node); },
            [&](const SemTry& node) noexcept {
                draft.add_failure_contribution(failures, node.residual_failures.term());
                for (const auto& arm : node.arms) {
                    const auto handler = draft.add_empty_failure_term();
                    for (const auto& range : arm.pattern_bounds) {
                        if (range.begin) {
                            draft.add_failure_contribution(handler, range.begin->failures.term());
                        }
                        if (range.end) {
                            draft.add_failure_contribution(handler, range.end->failures.term());
                        }
                    }
                    if (arm.guard.has_value()) {
                        draft.add_failure_contribution(handler, arm.guard->failures.term());
                    }
                    draft.add_failure_contribution(handler, arm.body.failures.term());
                    if (arm.body.result.has_value()) {
                        draft.add_failure_contribution(handler, arm.body.result->failures.term());
                    }
                    draft.add_guarded_failure_contribution(
                        failures,
                        arm.accepted_failures.term(),
                        handler
                    );
                }
            },
            }
    );
    return {
        .type = BodyType(type),
        .lifetime = lifetime,
        .origin = origin,
        .constant = constant,
        .failures = BodyFailures(failures),
        .exits_test = false,
        .operation_reachable = true,
        .category = SemanticValueCategory::Value,
        .value = std::move(value),

    };
}

auto BodyBuilder::binding_expression(LocalBindingID id) noexcept -> PlaceExpression {
    const auto binding = bindings.copy(id);
    auto expression =
        make_expression(binding.type, binding.lifetime, binding.origin, SemBinding {.binding = id});
    expression.category = SemanticValueCategory::Place;
    const auto access =
        binding.storage.visit([](const auto& storage) static noexcept -> AccessMode {
            using Storage = std::remove_cvref_t<decltype(storage)>;
            if constexpr (std::same_as<Storage, OwnerBindingStorage>) {
                return storage.writable ? AccessMode::Write : AccessMode::Read;
            } else if constexpr (std::same_as<Storage, ParameterBindingStorage>) {
                return storage.access == AccessMode::Write ? AccessMode::Write : AccessMode::Read;
            } else if constexpr (std::same_as<Storage, AsyncChildBindingStorage>) {
                return AccessMode::Read;
            } else {
                return storage.mode == CaptureMode::Write ? AccessMode::Write : AccessMode::Read;
            }
        });
    return {.root = id, .access = access, .expression = std::move(expression)};
}

auto BodyBuilder::remember_initializer(
    LocalBindingID id,
    const SemanticExpression& initializer
) noexcept -> void {
    const auto binding = bindings.copy(id);
    const auto* owner = std::get_if<OwnerBindingStorage>(&binding.storage);
    if (owner == nullptr || owner->writable) {
        return;
    }
    if (const auto callable = known_callable(initializer)) {
        local_callables.emplace(id, *callable);
    }
    if (const auto extent = known_sequence_extent(initializer)) {
        local_sequence_extents.emplace(id, *extent);
    }
}

auto BodyBuilder::known_callable(const SemanticExpression& expression) const noexcept
    -> std::optional<CallableID> {
    if (const auto* callable = std::get_if<SemCallable>(&expression.value)) {
        return callable->callable;
    }
    if (const auto* adaptation = std::get_if<SemBorrowCallable>(&expression.value)) {
        return known_callable(*adaptation->source);
    }
    if (const auto* binding = std::get_if<SemBinding>(&expression.value)) {
        const auto found = local_callables.find(binding->binding);
        if (found != local_callables.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

auto BodyBuilder::known_sequence_extent(const SemanticExpression& expression) const noexcept
    -> std::optional<std::uint64_t> {
    if (const auto constant = expression.constant) {
        const auto& fact = draft.constant(*constant);
        if (const auto* slice = std::get_if<SliceConstant>(&fact.value)) {
            return slice->elements.size();
        }
    }
    const auto& type = expression.type.construction();
    if (const auto* id = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*id);
        if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
            return array->extent;
        }
    } else {
        const auto construction = draft.construction_type_copy(std::get<TypeTermID>(type));
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            return array->extent;
        }
    }
    if (const auto* intrinsic = std::get_if<SemIntrinsic>(&expression.value)) {
        if (const auto* slice = std::get_if<SliceIntrinsicOperation>(&intrinsic->operation)) {
            return slice->result_extent;
        }
    }
    if (const auto* binding = std::get_if<SemBinding>(&expression.value)) {
        const auto found = local_sequence_extents.find(binding->binding);
        if (found != local_sequence_extents.end()) {
            return found->second;
        }
    }
    if (const auto* take = std::get_if<SemTake>(&expression.value)) {
        return known_sequence_extent(*take->place);
    }
    return std::nullopt;
}

auto BodyBuilder::make_place(
    std::optional<LocalBindingID> root,
    AccessMode access,
    ConstructionTypeRef type,
    SemanticExpressionValue&& value,
    ProgramOriginID origin
) noexcept -> PlaceExpression {
    auto expression = make_expression(type, lifetime(), origin, std::move(value));
    expression.category = SemanticValueCategory::Place;
    return {.root = root, .access = access, .expression = std::move(expression)};
}

auto BodyBuilder::callable_expression(CallableID callable, ProgramOriginID origin) noexcept
    -> SemanticExpression {
    const auto type =
        draft.intern_type(CanonicalType {.value = FunctionTypeValue {.callable = callable}});
    return make_expression(type, lifetime(), origin, SemCallable {.callable = callable});
}

auto BodyBuilder::finish(SemanticRegion region) && noexcept -> StructuredBodyDraft {
    return {
        .id = body_id,
        .kind = body_kind,
        .provenance_identity = provenance_identity,
        .inputs = std::move(body_inputs),
        .lifetime_regions = LifetimeRegionTree(std::move(lifetime_regions).seal()),
        .bindings = std::move(bindings).seal(),
        .patterns = std::move(patterns).seal(),
        .region = std::move(region),
        .residual = std::nullopt,
        .specialized = std::nullopt,
    };
}

auto BodyBuilder::cpp_place(
    PlaceExpression source,
    ConstructionTypeRef type,
    CppOperation operation,
    std::vector<SemCallArgument> operands,
    ProgramOriginID origin
) noexcept -> PlaceExpression {
    const auto root = source.root;
    const auto access = source.access;
    const auto operation_reachable = source.expression.operation_reachable;
    operands.insert(
        operands.begin(),
        {.access = access, .expression = std::move(source.expression)}
    );
    auto expression = make_expression(
        type,
        lifetime(),
        origin,
        SemCpp {.operation = std::move(operation), .operands = std::move(operands)}
    );
    expression.operation_reachable = operation_reachable;
    expression.category = SemanticValueCategory::Place;
    return {.root = root, .access = access, .expression = std::move(expression)};
}

auto BodyBuilder::identity() const noexcept -> BodyIdentity {
    return body_identity;
}

auto BodyBuilder::id() const noexcept -> BodyID {
    return body_id;
}

auto BodyBuilder::kind() const noexcept -> BodyKind {
    return body_kind;
}

auto BodyBuilder::set_lifetime(LifetimeRegionID lifetime) noexcept -> void {
    active_lifetime = lifetime;
}

auto BodyBuilder::lifetime() const noexcept -> LifetimeRegionID {
    return active_lifetime.value();
}

auto BodyBuilder::pattern_table() const noexcept
    -> const MutableBodyTable<ElaboratedPattern, PatternID>& {
    return patterns;
}
