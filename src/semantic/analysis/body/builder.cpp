module carven:semantic.analysis.body.builder.impl;

import :semantic.analysis.body.builder;
import :semantic.semir.decl;
import :support.visit;
import std;

BodyBuilder::BodyBuilder(BodyReservation&& reservation, ProgramDraft& draft) noexcept
    : BodyBuilder(reservation.consume(), draft) {}
BodyBuilder::BodyBuilder(BodyReservation::Consumed reservation, ProgramDraft& draft) noexcept
    : draft(draft),
      body_identity(reservation.id.owner(), reservation.id.index()),
      body_id(reservation.id),
      body_kind(reservation.kind),
      provenance_identity(reservation.provenance),
      scopes(body_identity),
      lifetime_regions(body_identity),
      bindings(body_identity),
      patterns(body_identity) {}

auto BodyBuilder::make_expression(
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    ProgramOriginID origin,
    DraftExpressionValue value,
    std::optional<ConstantID> constant
) noexcept -> DraftExpression {
    auto failures = draft.add_empty_failure_term();
    auto exits_test = false;
    const auto add = [&](const DraftExpression& child) noexcept {
        draft.add_failure_contribution(failures, child.failures);
        exits_test |= child.exits_test;
    };
    const auto add_region = [&](const DraftRegion& region) noexcept {
        draft.add_failure_contribution(failures, region.failures);
        exits_test |= region.exits_test;
        if (region.result.has_value()) {
            add(*region.result);
        }
    };
    const auto truth = [&](const DraftExpression& expression) noexcept -> std::optional<bool> {
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
            [](const SemLiteral&) static noexcept {},
            [](const SemConstant&) static noexcept {},
            [](const SemBinding&) static noexcept {},
            [](const SemCallable&) static noexcept {},
            [](const SemEnumConstructor&) static noexcept {},
            [&](const SemCpp<ConstructionTypeRef, FailureTermID>& node) noexcept {
                for (const auto& operand : node.operands) {
                    add(operand.expression);
                }
            },
            [&](const SemSequence<ConstructionTypeRef, FailureTermID>& node) noexcept {
                for (const auto& child : node.expressions) {
                    add(child);
                }
            },
            [&](const SemArray<ConstructionTypeRef, FailureTermID>& node) noexcept {
                for (const auto& child : node.elements) {
                    add(child);
                }
            },
            [&](const SemArrayAdopt<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.source);
            },
            [&](const SemStruct<ConstructionTypeRef, FailureTermID>& node) noexcept {
                for (const auto& field : node.fields) {
                    add(field.value);
                }
            },
            [&](const SemEnumCase<ConstructionTypeRef, FailureTermID>& node) noexcept {
                for (const auto& child : node.payload) {
                    add(child);
                }
            },
            [&](const SemUnary<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.operand);
            },
            [&](const SemBinary<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.left);
                add(*node.right);
            },
            [&](const SemShortCircuit<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.left);
                const auto known = truth(*node.left);
                if (!known.has_value() || *known == (node.operation == ShortCircuitOperator::And)) {
                    add(*node.right);
                }
            },
            [&](const SemCast<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.operand);
            },
            [&](const SemField<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.source);
            },
            [&](const SemIndex<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.source);
                add(*node.index);
            },
            [&](const SemTextIntrinsic<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.source);
            },
            [&](const SemCall<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.callee);
                for (const auto& argument : node.arguments) {
                    add(argument.expression);
                }
                draft.add_failure_contribution(failures, node.callee_failures);
            },
            [&](const SemClosure<ConstructionTypeRef, FailureTermID>& node) noexcept {
                for (const auto& capture : node.captures) {
                    add(capture.expression);
                }
            },
            [&](const SemBorrowCallable<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.source);
            },
            [&](const SemTake<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.place);
            },
            [&](const SemPropagate<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(*node.operand);
            },
            [&](const SemIf<ConstructionTypeRef, FailureTermID>& node) noexcept {
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
            [&](const SemMatch<ConstructionTypeRef, FailureTermID>& node) noexcept {
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
            [&](const SemTry<ConstructionTypeRef, FailureTermID>& node) noexcept {
                draft.add_failure_contribution(failures, node.residual_failures);
                exits_test |= node.body->exits_test;
                if (node.body->result.has_value()) {
                    exits_test |= node.body->result->exits_test;
                }
                for (const auto& arm : node.arms) {
                    const auto handler = draft.add_empty_failure_term();
                    auto executes_body = true;
                    if (arm.guard.has_value()) {
                        draft.add_failure_contribution(handler, arm.guard->failures);
                        exits_test |= arm.guard->exits_test;
                        const auto known = truth(*arm.guard);
                        executes_body = !known.has_value() || *known;
                    }
                    if (executes_body) {
                        draft.add_failure_contribution(handler, arm.body.failures);
                        exits_test |= arm.body.exits_test;
                        if (arm.body.result.has_value()) {
                            draft.add_failure_contribution(handler, arm.body.result->failures);
                            exits_test |= arm.body.result->exits_test;
                        }
                    }
                    draft
                        .add_guarded_failure_contribution(failures, arm.accepted_failures, handler);
                }
            },
        },
        value
    );
    return {
        .type = type,
        .lifetime = lifetime,
        .origin = origin,
        .constant = constant,
        .failures = failures,
        .exits_test = exits_test,
        .category = SemanticValueCategory::Value,
        .value = std::move(value)
    };
}
auto BodyBuilder::add_expression(DraftExpression expression) noexcept -> ExpressionHandle {
    if (values.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("expression construction exhausted its handle space");
    }
    const auto id = ExpressionHandle(body_identity, static_cast<std::uint32_t>(values.size()));
    values.emplace_back(std::move(expression));
    return id;
}
auto BodyBuilder::take_value(ExpressionHandle id) noexcept -> DraftExpression {
    if (id.owner() != body_identity
        || static_cast<std::size_t>(id.index()) >= values.size()
        || !values[id.index()].has_value()) {
        invariant_violation("expression handle was foreign or already consumed");
    }
    auto value = std::move(*values[id.index()]);
    values[id.index()].reset();
    return value;
}
auto BodyBuilder::root_expression(LocalBindingID id) noexcept -> DraftExpression {
    const auto binding = bindings.copy(id);
    auto result =
        make_expression(binding.type, binding.lifetime, binding.origin, SemBinding {.binding = id});
    result.category = SemanticValueCategory::Place;
    return result;
}
auto BodyBuilder::add_place(PlaceConstruction place) noexcept -> PlaceHandle {
    if (places.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("place construction exhausted its handle space");
    }
    const auto id = PlaceHandle(body_identity, static_cast<std::uint32_t>(places.size()));
    places.push_back(std::move(place));
    return id;
}
auto BodyBuilder::place_root(PlaceHandle id) const noexcept -> LocalBindingID {
    if (id.owner() != body_identity || static_cast<std::size_t>(id.index()) >= places.size()) {
        invariant_violation("foreign place handle");
    }
    return std::visit(
        Overloaded {
            [](LocalBindingID root) static noexcept { return root; },
            [](const ProjectedPlace& place) static noexcept { return place.root; },
            [](ConsumedPlace) static noexcept -> LocalBindingID {
                invariant_violation("projected place was consumed more than once");
            },
        },
        places[id.index()]
    );
}
auto BodyBuilder::take_place(PlaceHandle id) noexcept -> DraftExpression {
    const auto root = place_root(id);
    auto& place = places[id.index()];
    if (auto* projected = std::get_if<ProjectedPlace>(&place)) {
        auto result = std::move(projected->expression);
        place = ConsumedPlace {};
        return result;
    }
    // Root places identify bindings, not evaluations, and may be referenced anew.
    return root_expression(root);
}
auto BodyBuilder::callable_expression(CallableID callable, ProgramOriginID origin) noexcept
    -> DraftExpression {
    const auto type =
        draft.intern_type(CanonicalType {.value = FunctionTypeValue {.callable = callable}});
    return make_expression(type, lifetime(), origin, SemCallable {.callable = callable});
}
auto BodyBuilder::call_expression(
    expression_construction::Callee callee,
    const std::vector<expression_construction::Argument>& arguments,
    ConstructionTypeRef type,
    FailureTermID failures,
    ProgramOriginID origin
) noexcept -> DraftExpression {
    auto target = std::visit(
        Overloaded {
            [&](CallableID id) noexcept { return callable_expression(id, origin); },
            [&](ExpressionHandle id) noexcept { return take_value(id); }
        },
        callee
    );
    auto args = std::vector<SemCallArgument<ConstructionTypeRef, FailureTermID>>();
    for (const auto& argument : arguments) {
        args.push_back(
            std::visit(
                Overloaded {
                    [&](expression_construction::ReadArgument value) noexcept {
                        return SemCallArgument<ConstructionTypeRef, FailureTermID> {
                            AccessMode::Read,
                            take_value(value.value)
                        };
                    },
                    [&](expression_construction::TakeArgument value) noexcept {
                        return SemCallArgument<ConstructionTypeRef, FailureTermID> {
                            AccessMode::Take,
                            take_value(value.value)
                        };
                    },
                    [&](expression_construction::WriteArgument value) noexcept {
                        return SemCallArgument<ConstructionTypeRef, FailureTermID> {
                            AccessMode::Write,
                            take_place(value.place)
                        };
                    }
                },
                argument
            )
        );
    }
    auto result = make_expression(
        type,
        lifetime(),
        origin,
        SemCall<ConstructionTypeRef, FailureTermID> {
            .callee = UniqueIndirect(std::move(target)),
            .arguments = std::move(args),
            .callee_failures = failures
        }
    );
    return result;
}
auto BodyBuilder::append_value(
    ConstructionTypeRef type,
    LifetimeRegionID region,
    expression_construction::Input operation,
    ProgramOriginID origin
) noexcept -> ExpressionHandle {
    if (const auto* read = std::get_if<expression_construction::Read>(&operation)) {
        auto expression = take_place(read->place);
        expression.category = SemanticValueCategory::Value;
        expression.lifetime = region;
        expression.origin = origin;
        return add_expression(std::move(expression));
    }
    auto value = std::visit(
        [&](auto&& op) noexcept -> DraftExpressionValue {
            using Op = std::remove_cvref_t<decltype(op)>;
            if constexpr (std::same_as<Op, SemLiteral>) {
                return SemLiteral {op.value};
            } else if constexpr (std::same_as<Op, SemConstant>) {
                return SemConstant {op.constant};
            } else if constexpr (std::same_as<Op, SemEnumConstructor>) {
                return SemEnumConstructor {op.enum_case};
            } else if constexpr (std::same_as<Op, expression_construction::Array>) {
                auto elements = std::vector<DraftExpression>();
                for (const auto id : op.elements) {
                    elements.push_back(take_value(id));
                }
                return SemArray<ConstructionTypeRef, FailureTermID> {std::move(elements)};
            } else if constexpr (std::same_as<Op, expression_construction::Struct>) {
                auto fields =
                    std::vector<SemFieldInitializer<ConstructionTypeRef, FailureTermID>>();
                for (const auto& field : op.fields) {
                    fields.push_back({field.declaration_index, take_value(field.value)});
                }
                return SemStruct<ConstructionTypeRef, FailureTermID> {
                    op.structure,
                    std::move(fields)
                };
            } else if constexpr (std::same_as<Op, expression_construction::EnumCase>) {
                auto payload = std::vector<DraftExpression>();
                for (const auto id : op.payload) {
                    payload.push_back(take_value(id));
                }
                return SemEnumCase<ConstructionTypeRef, FailureTermID> {
                    op.enum_case,
                    std::move(payload)
                };
            } else if constexpr (std::same_as<Op, expression_construction::Unary>) {
                return SemUnary<ConstructionTypeRef, FailureTermID> {
                    op.operation,
                    UniqueIndirect(take_value(op.operand))
                };
            } else if constexpr (std::same_as<Op, expression_construction::Binary>) {
                return SemBinary<ConstructionTypeRef, FailureTermID> {
                    UniqueIndirect(take_value(op.left)),
                    op.operation,
                    UniqueIndirect(take_value(op.right))
                };
            } else if constexpr (std::same_as<Op, expression_construction::Cast>) {
                return SemCast<ConstructionTypeRef, FailureTermID> {
                    UniqueIndirect(take_value(op.operand)),
                    op.kind
                };
            } else if constexpr (std::same_as<Op, expression_construction::Project>) {
                auto source = take_value(op.source);
                return std::visit(
                    [&](auto projection) noexcept -> DraftExpressionValue {
                        using Projection = decltype(projection);
                        if constexpr (std::same_as<Projection, FieldProjection>) {
                            return SemField<ConstructionTypeRef, FailureTermID> {
                                UniqueIndirect(std::move(source)),
                                {projection.owner, projection.field_index}
                            };
                        } else if constexpr (std::same_as<
                                                 Projection,
                                                 expression_construction::IndexProjection>) {
                            return SemIndex<ConstructionTypeRef, FailureTermID> {
                                UniqueIndirect(std::move(source)),
                                UniqueIndirect(take_value(projection.index)),
                                projection.bounds
                            };
                        } else {
                            invariant_violation(
                                "source expression unexpectedly projected an enum payload"
                            );
                        }
                    },
                    op.projection
                );
            } else if constexpr (std::same_as<Op, expression_construction::TextOperation>) {
                return SemTextIntrinsic<ConstructionTypeRef, FailureTermID> {
                    UniqueIndirect(take_value(op.source)),
                    op.intrinsic
                };
            } else if constexpr (std::same_as<Op, expression_construction::Closure>) {
                auto captures = std::vector<SemCapture<ConstructionTypeRef, FailureTermID>>();
                for (const auto& capture : op.captures) {
                    captures.push_back(
                        std::visit(
                            Overloaded {
                                [&](expression_construction::ValueCapture value) noexcept {
                                    return SemCapture<ConstructionTypeRef, FailureTermID> {
                                        CaptureMode::Value,
                                        take_value(value.value)
                                    };
                                },
                                [&](expression_construction::WriteCapture value) noexcept {
                                    return SemCapture<ConstructionTypeRef, FailureTermID> {
                                        CaptureMode::Write,
                                        take_place(value.place)
                                    };
                                }
                            },
                            capture
                        )
                    );
                }
                return SemClosure<ConstructionTypeRef, FailureTermID> {
                    op.callable,
                    std::move(captures)
                };
            } else if constexpr (std::same_as<Op, expression_construction::Take>) {
                return SemTake<ConstructionTypeRef, FailureTermID> {
                    UniqueIndirect(take_place(op.place))
                };
            } else if constexpr (std::same_as<Op, expression_construction::Read>) {
                invariant_violation("read place is handled before operation conversion");
            } else {
                invariant_violation(
                    "analysis-only operation entered source expression construction"
                );
            }
        },
        std::move(operation)
    );
    auto expression = make_expression(type, region, origin, std::move(value));
    if (const auto* constant = std::get_if<SemConstant>(&expression.value)) {
        expression.constant = constant->constant;
    }
    return add_expression(std::move(expression));
}
auto BodyBuilder::append_place(
    PlaceHandle base,
    ConstructionTypeRef type,
    expression_construction::Projection projection,
    ProgramOriginID origin
) noexcept -> PlaceHandle {
    const auto root = place_root(base);
    auto source = take_place(base);
    auto value = std::visit(
        Overloaded {
            [&](FieldProjection field) noexcept -> DraftExpressionValue {
                return SemField<ConstructionTypeRef, FailureTermID> {
                    UniqueIndirect(std::move(source)),
                    field
                };
            },
            [&](expression_construction::IndexProjection index) noexcept -> DraftExpressionValue {
                return SemIndex<ConstructionTypeRef, FailureTermID> {
                    UniqueIndirect(std::move(source)),
                    UniqueIndirect(take_value(index.index)),
                    index.bounds
                };
            }
        },
        projection
    );
    auto expression = make_expression(type, lifetime(), origin, std::move(value));
    expression.category = SemanticValueCategory::Place;
    return add_place(ProjectedPlace {.root = root, .expression = std::move(expression)});
}
auto BodyBuilder::append_unresolved_callable_borrow(
    ConstructionTypeRef type,
    LifetimeRegionID region,
    UnresolvedCallableBorrowOp op,
    ProgramOriginID origin
) noexcept -> ExpressionHandle {
    draft.require_failure_subset(
        op.source_failure_term_id,
        op.target_failure_term_id,
        origin,
        FailureSubsetRequirementKind::CallableAdoption
    );
    auto source = std::visit(
        Overloaded {
            [&](CallableID id) noexcept { return callable_expression(id, origin); },
            [&](ExpressionHandle id) noexcept { return take_value(id); },
            [&](PlaceHandle id) noexcept { return take_place(id); }
        },
        op.backing
    );
    return add_expression(make_expression(
        type,
        region,
        origin,
        SemBorrowCallable<ConstructionTypeRef, FailureTermID> {
            UniqueIndirect(std::move(source)),
            op.loan_lifetime
        }
    ));
}

auto BodyBuilder::finish(DraftRegion region) && noexcept -> StructuredBodyDraft {
    return {
        .id = body_id,
        .kind = body_kind,
        .provenance_identity = provenance_identity,
        .inputs = std::move(body_inputs),
        .scopes = ScopeTree(std::move(scopes).seal()),
        .lifetime_regions = LifetimeRegionTree(std::move(lifetime_regions).seal()),
        .bindings = std::move(bindings).seal(),
        .patterns = std::move(patterns).seal(),
        .region = std::move(region)
    };
}

auto BodyBuilder::append_cpp_place(
    PlaceHandle source,
    ConstructionTypeRef type,
    CppOperation operation,
    std::vector<SemCallArgument<ConstructionTypeRef, FailureTermID>> operands,
    ProgramOriginID origin
) noexcept -> PlaceHandle {
    const auto root = place_root(source);
    operands.insert(
        operands.begin(),
        {.access = place_access(source), .expression = take_place(source)}
    );
    auto expression = make_expression(
        type,
        lifetime(),
        origin,
        SemCpp<ConstructionTypeRef, FailureTermID> {
            .operation = std::move(operation),
            .operands = std::move(operands)
        }
    );
    expression.category = SemanticValueCategory::Place;
    return add_place(ProjectedPlace {.root = root, .expression = std::move(expression)});
}
