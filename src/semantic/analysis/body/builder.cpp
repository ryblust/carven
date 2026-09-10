module carven:semantic.analysis.body.builder.impl;

import :semantic.analysis.body.builder;
import :semantic.analysis.program;
import :semantic.semir.decl;
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
            const auto fact = draft.constant_copy(*expression.constant);
            if (const auto* boolean = std::get_if<BooleanConstant>(&fact.value)) {
                return boolean->value;
            }
        }
        return std::nullopt;
    };
    std::visit(
        Overloaded {
            [](const SemConstant&) static noexcept {},
            [](const SemBinding&) static noexcept {},
            [](const SemCallable&) static noexcept {},
            [](const SemEnumConstructor&) static noexcept {},
            [&](const SemCpp& value) noexcept {
                visit_cpp_operands(value, [&](AccessMode, const auto& expression) noexcept {
                    add(expression);
                });
            },
            [&](const SemCppCall& value) noexcept {
                visit_cpp_operands(value, [&](AccessMode, const auto& expression) noexcept {
                    add(expression);
                });
            },
            [&](const SemArray& node) noexcept {
                for (const auto& child : node.elements) {
                    add(child);
                }
            },
            [&](const SemArrayAdopt& node) noexcept { add(*node.source); },
            [&](const SemStruct& node) noexcept {
                for (const auto& field : node.fields) {
                    add(field.value);
                }
            },
            [&](const SemEnumCase& node) noexcept {
                for (const auto& child : node.payload) {
                    add(child);
                }
            },
            [&](const SemUnary& node) noexcept { add(*node.operand); },
            [&](const SemBinary& node) noexcept {
                add(*node.left);
                add(*node.right);
            },
            [&](const SemShortCircuit& node) noexcept {
                add(*node.left);
                const auto known = truth(*node.left);
                if (!known.has_value() || *known == (node.operation == ShortCircuitOperator::And)) {
                    add(*node.right);
                }
            },
            [&](const SemDereference& node) noexcept { add(*node.source); },
            [&](const SemCast& node) noexcept { add(*node.operand); },
            [&](const SemField& node) noexcept { add(*node.source); },
            [&](const SemIndex& node) noexcept {
                add(*node.source);
                add(*node.index);
            },
            [&](const SemFormat& node) noexcept {
                for (const auto& operand : node.operands) {
                    add(operand.expression);
                }
            },
            [&](const SemTextIntrinsic& node) noexcept {
                for (const auto& operand : node.operands) {
                    add(operand.expression);
                }
            },
            [&](const SemCall& node) noexcept {
                add(*node.callee);
                for (const auto& argument : node.arguments) {
                    add(argument.expression);
                }
                draft.add_failure_contribution(failures, node.callee_failures.term());
            },
            [&](const SemClosure& node) noexcept {
                for (const auto& capture : node.captures) {
                    add(capture.expression);
                }
            },
            [&](const SemBorrowCallable& node) noexcept { add(*node.source); },
            [&](const SemTake& node) noexcept { add(*node.place); },
            [&](const SemPropagate& node) noexcept { add(*node.operand); },
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
        },
        value
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
