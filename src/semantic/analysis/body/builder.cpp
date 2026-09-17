module carven:semantic.analysis.body.builder.impl;

import :semantic.analysis.body.builder;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.children;
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

auto BodyBuilder::make_expression(
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    ProgramOriginID origin,
    SemanticExpressionValue value,
    std::optional<ConstantID> constant
) noexcept -> SemanticExpression {
    if (const auto* known = std::get_if<SemConstant>(&value)) {
        constant = known->constant;
    }
    auto failures = draft.add_empty_failure_term();
    auto exits_test = false;
    const auto add = [&](const SemanticExpression& child) noexcept {
        draft.add_failure_contribution(failures, child.failures.term());
        exits_test |= child.exits_test;
    };
    const auto add_region = [&](const SemanticRegion& region) noexcept {
        draft.add_failure_contribution(failures, region.failures.term());
        exits_test |= region.exits_test;
        if (region.result.has_value()) {
            add(*region.result);
        }
    };
    const auto truth = [&](const SemanticExpression& expression) noexcept -> std::optional<bool> {
        if (expression.constant.has_value()) {
            const auto& fact = draft.constant(*expression.constant);
            if (const auto* boolean = std::get_if<BooleanConstant>(&fact.value)) {
                return boolean->value;
            }
        }
        return std::nullopt;
    };
    value.visit(
        Overloaded {
            [&](const SemShortCircuit& node) noexcept {
                add(*node.left);
                const auto known = truth(*node.left);
                if (!known.has_value() || *known == (node.operation == ShortCircuitOperator::And)) {
                    add(*node.right);
                }
            },
            [&]<typename Operation>(const Operation& node) noexcept
                requires std::same_as<Operation, SemConstant>
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
                             || std::same_as<Operation, SemField>
                             || std::same_as<Operation, SemIndex>
                             || std::same_as<Operation, SemPrint>
                             || std::same_as<Operation, SemFormat>
                             || std::same_as<Operation, SemSliceIntrinsic>
                             || std::same_as<Operation, SemTextIntrinsic>
                             || std::same_as<Operation, SemClosure>
                             || std::same_as<Operation, SemBorrowCallable>
                             || std::same_as<Operation, SemTake>
                             || std::same_as<Operation, SemPropagate>
            { visit_semantic_children(node, add); },
            [&](const SemCall& node) noexcept {
                visit_semantic_children(node, add);
                draft.add_failure_contribution(failures, node.callee_failures.term());
            },
            [&](const SemTestReport& node) noexcept {
                visit_semantic_children(node, add);
                exits_test |= node.kind != TestReportKind::Check;
            },
            [&](const SemIf& node) noexcept {
                auto remaining = true;
                for (const auto& branch : node.branches) {
                    if (!remaining) {
                        break;
                    }
                    add(branch.condition);
                    const auto known = truth(branch.condition);
                    if (!known.has_value() || *known) {
                        add_region(branch.body);
                    }
                    remaining = !known.has_value() || !*known;
                }
                if (remaining && node.otherwise.has_value()) {
                    add_region(**node.otherwise);
                }
            },
            [&](const SemMatch& node) noexcept {
                add(*node.subject);
                for (const auto& arm : node.arms) {
                    if (!arm.reachable) {
                        continue;
                    }
                    for (const auto& range : arm.pattern_bounds) {
                        if (range.begin) {
                            add(*range.begin);
                        }
                        if (range.end) {
                            add(*range.end);
                        }
                    }
                    if (arm.guard.has_value()) {
                        add(*arm.guard);
                        const auto known = truth(*arm.guard);
                        if (known.has_value() && !*known) {
                            continue;
                        }
                    }
                    add_region(arm.body);
                }
            },
            [&](const SemTry& node) noexcept {
                draft.add_failure_contribution(failures, node.residual_failures.term());
                exits_test |= node.body->exits_test;
                if (node.body->result.has_value()) {
                    exits_test |= node.body->result->exits_test;
                }
                for (const auto& arm : node.arms) {
                    const auto handler = draft.add_empty_failure_term();
                    auto executes_body = true;
                    for (const auto& range : arm.pattern_bounds) {
                        if (range.begin) {
                            draft.add_failure_contribution(handler, range.begin->failures.term());
                            exits_test |= range.begin->exits_test;
                        }
                        if (range.end) {
                            draft.add_failure_contribution(handler, range.end->failures.term());
                            exits_test |= range.end->exits_test;
                        }
                    }
                    if (arm.guard.has_value()) {
                        draft.add_failure_contribution(handler, arm.guard->failures.term());
                        exits_test |= arm.guard->exits_test;
                        const auto known = truth(*arm.guard);
                        executes_body = !known.has_value() || *known;
                    }
                    if (executes_body) {
                        draft.add_failure_contribution(handler, arm.body.failures.term());
                        exits_test |= arm.body.exits_test;
                        if (arm.body.result.has_value()) {
                            draft.add_failure_contribution(
                                handler,
                                arm.body.result->failures.term()
                            );
                            exits_test |= arm.body.result->exits_test;
                        }
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
        .exits_test = exits_test,
        .category = SemanticValueCategory::Value,
        .value = std::move(value)
    };
}

auto BodyBuilder::binding_expression(LocalBindingID id) noexcept -> PlaceExpression {
    const auto binding = bindings.copy(id);
    auto expression =
        make_expression(binding.type, binding.lifetime, binding.origin, SemBinding {.binding = id});
    expression.category = SemanticValueCategory::Place;
    return {.root = id, .expression = std::move(expression)};
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
    if (const auto extent = known_sequence_extent(initializer)) {
        local_sequence_extents.emplace(id, *extent);
    }
    const auto constant = known_constant(initializer);
    if (!constant) {
        return;
    }
    const auto& fact = draft.constant(*constant);
    const auto type = draft.type_copy(fact.type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
    if (std::holds_alternative<RangeTypeValue>(type.value)) {
        local_constants.emplace(id, *constant);
    }
    if (builtin != nullptr
        && (builtin_is_integer(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char
            || builtin->kind == BuiltinType::Str)) {
        local_constants.emplace(id, *constant);
    }
}

auto BodyBuilder::known_constant(const SemanticExpression& expression) const noexcept
    -> std::optional<ConstantID> {
    if (expression.constant) {
        return expression.constant;
    }
    if (const auto* binding = std::get_if<SemBinding>(&expression.value)) {
        const auto found = local_constants.find(binding->binding);
        if (found != local_constants.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

auto BodyBuilder::known_sequence_extent(const SemanticExpression& expression) const noexcept
    -> std::optional<std::uint64_t> {
    if (const auto constant = known_constant(expression)) {
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
    if (const auto* slice = std::get_if<SemSliceIntrinsic>(&expression.value)) {
        return slice->result_extent;
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
    ConstructionTypeRef type,
    SemanticExpressionValue value,
    ProgramOriginID origin
) noexcept -> PlaceExpression {
    auto expression = make_expression(type, lifetime(), origin, std::move(value));
    expression.category = SemanticValueCategory::Place;
    return {.root = root, .expression = std::move(expression)};
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
        .region = std::move(region)
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
    operands.insert(
        operands.begin(),
        {.access = place_access(source), .expression = std::move(source.expression)}
    );
    auto expression = make_expression(
        type,
        lifetime(),
        origin,
        SemCpp {.operation = std::move(operation), .operands = std::move(operands)}
    );
    expression.category = SemanticValueCategory::Place;
    return {.root = root, .expression = std::move(expression)};
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
