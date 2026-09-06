module carven:semantic.analysis.ownership.expr.impl;

import :semantic.analysis.ownership.context;
import std;

namespace ownership {

auto BodyAnalyzer::place(const SemIRExpression& source, State state, bool read) noexcept -> Flow {
    auto result = Flow {.normal = std::move(state), .value = {}, .exits = {}};
    if (const auto* foreign = std::get_if<SemCpp<TypeID, FailureSetID>>(&source.value)) {
        result = place(foreign->operands.front().expression, std::move(*result.normal));
        for (const auto& operand : std::span(foreign->operands).subspan(1)) {
            if (!result.normal.has_value()) {
                break;
            }
            const auto previous = accesses.size();
            accesses.push_back({*location(foreign->operands.front().expression), false});
            auto next = expression(operand.expression, std::move(*result.normal));
            accesses.resize(previous);
            result.normal = std::move(next.normal);
            append_exits(result, next);
        }
    } else if (const auto* field = std::get_if<SemField<TypeID, FailureSetID>>(&source.value)) {
        result = place(*field->source, std::move(*result.normal));
    } else if (const auto* index = std::get_if<SemIndex<TypeID, FailureSetID>>(&source.value)) {
        result = place(*index->source, std::move(*result.normal));
        if (result.normal.has_value()) {
            const auto previous = accesses.size();
            accesses.push_back({*location(*index->source), false});
            auto constant_index = expression(*index->index, std::move(*result.normal));
            accesses.resize(previous);
            result.normal = std::move(constant_index.normal);
            append_exits(result, constant_index);
        }
    }
    if (result.normal.has_value()) {
        const auto target = location(source);
        if (!target.has_value()) {
            invariant_violation("semantic place has no storage location");
        }
        if (read) {
            require_available(*result.normal, *target, source.origin);
        }
        result.value = project(result.normal->objects[target->object].relationships, target->path);
        if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
            const auto* parameter =
                std::get_if<ParameterBindingStorage>(&body.binding(binding->binding).storage);
            if (parameter != nullptr
                && parameter->access == AccessMode::Read
                && aliases.contains(binding->binding)) {
                merge_relationships(
                    result.value,
                    result.normal->objects[input.objects.size() + binding->binding.index()]
                        .relationships
                );
            }
        }
    }
    return result;
}
auto BodyAnalyzer::expression(const SemIRExpression& source, State state, bool direct) noexcept
    -> Flow {
    auto flow = Flow {.normal = std::move(state), .value = {}, .exits = {}};
    const auto evaluate = [&](const SemIRExpression& child,
                              bool argument = false) noexcept -> Relationships {
        if (!flow.normal.has_value()) {
            return {};
        }
        auto next = expression(child, std::move(*flow.normal), argument);
        flow.normal = std::move(next.normal);
        append_exits(flow, next);
        return std::move(next.value);
    };
    const auto aggregate = [&](const SemIRExpression& child,
                               const ProjectionPath& path = {}) noexcept {
        merge_relationships(flow.value, nested(evaluate(child), path));
    };
    const auto callable_for = [&](TypeID type) noexcept -> std::optional<CallableID> {
        const auto value = draft.type_copy(type).value;
        if (const auto* closure = std::get_if<ClosureTypeValue>(&value)) {
            return closure->callable;
        }
        if (const auto* function = std::get_if<FunctionTypeValue>(&value)) {
            return function->callable;
        }
        return std::nullopt;
    };
    if (source.category == SemanticValueCategory::Place) {
        flow = place(source, std::move(*flow.normal));
    } else {
        std::visit(
            Overloaded {
                [](const SemLiteral&) static noexcept {},
                [](const SemConstant&) static noexcept {},
                [&](const SemBinding&) noexcept { flow = place(source, std::move(*flow.normal)); },
                [&](const SemCallable& value) noexcept {
                    flow.value.loans.push_back(
                        {{}, std::nullopt, value.callable, source.origin, false}
                    );
                },
                [](const SemEnumConstructor&) static noexcept {},
                [&](const SemSequence<TypeID, FailureSetID>& value) noexcept {
                    for (const auto& child : value.expressions) {
                        flow.value = evaluate(child);
                    }
                },
                [&](const SemArray<TypeID, FailureSetID>& value) noexcept {
                    for (const auto [index, child] : std::views::enumerate(value.elements)) {
                        aggregate(child, ProjectionPath {index});
                    }
                },
                [&](const SemStruct<TypeID, FailureSetID>& value) noexcept {
                    for (const auto& field : value.fields) {
                        aggregate(field.value, ProjectionPath {field.declaration_index});
                    }
                },
                [&](const SemEnumCase<TypeID, FailureSetID>& value) noexcept {
                    for (const auto [index, child] : std::views::enumerate(value.payload)) {
                        aggregate(child, ProjectionPath {index});
                    }
                },
                [&](const SemUnary<TypeID, FailureSetID>& value) noexcept {
                    static_cast<void>(evaluate(*value.operand));
                },
                [&](const SemBinary<TypeID, FailureSetID>& value) noexcept {
                    static_cast<void>(evaluate(*value.left));
                    static_cast<void>(evaluate(*value.right));
                },
                [&](const SemCast<TypeID, FailureSetID>& value) noexcept {
                    aggregate(*value.operand);
                },
                [&](const SemShortCircuit<TypeID, FailureSetID>& value) noexcept {
                    static_cast<void>(evaluate(*value.left));
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    const auto known = constant_truth(*value.left);
                    const auto selected = value.operation == ShortCircuitOperator::And;
                    if (known.has_value() && *known != selected) {
                        return;
                    }
                    const auto skipped = flow.normal;
                    static_cast<void>(evaluate(*value.right));
                    if (!known.has_value()) {
                        join_normal(flow.normal, skipped);
                    }
                },
                [&](const SemField<TypeID, FailureSetID>& value) noexcept {
                    flow.value =
                        project(evaluate(*value.source), ProjectionPath {value.field.field_index});
                },
                [&](const SemIndex<TypeID, FailureSetID>& value) noexcept {
                    const auto relationships = evaluate(*value.source);
                    static_cast<void>(evaluate(*value.index));
                    flow.value =
                        project(relationships, ProjectionPath {constant_index(*value.index)});
                },
                [&](const SemTextIntrinsic<TypeID, FailureSetID>& value) noexcept {
                    static_cast<void>(evaluate(*value.source));
                },
                [&](const SemArrayAdopt<TypeID, FailureSetID>& value) noexcept {
                    const auto original = evaluate(*value.source);
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    const auto source_place = location(*value.source);
                    const auto backing = source_place.has_value()
                        ? *source_place
                        : Place {temporaries.at(std::addressof(*value.source)), {}};
                    const auto adopt = [&](this const auto& self,
                                           TypeID from,
                                           TypeID to,
                                           const ProjectionPath& path) noexcept -> void {
                        if (from == to) {
                            merge_relationships(flow.value, nested(project(original, path), path));
                            return;
                        }
                        const auto target = draft.type_copy(to).value;
                        if (const auto* array = std::get_if<ArrayTypeValue>(&target)) {
                            const auto input =
                                std::get<ArrayTypeValue>(draft.type_copy(from).value);
                            for (auto index = 0uz; index < array->extent; ++index) {
                                auto element = path;
                                element.push_back(index);
                                self(input.element, array->element, element);
                            }
                            return;
                        }
                        auto element = backing;
                        element.path.insert(element.path.end(), path.begin(), path.end());
                        flow.value.loans.push_back(
                            {path,
                             std::move(element),
                             callable_for(from),
                             source.origin,
                             !source_place.has_value()}
                        );
                    };
                    adopt(value.source->type, source.type, {});
                },
                [&](const SemBorrowCallable<TypeID, FailureSetID>& value) noexcept {
                    if (std::holds_alternative<SemTake<TypeID, FailureSetID>>(value.source->value)
                        && type_contents.contains_view(value.source->type)
                        && value.source->type != source.type) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "taken callable storage cannot back a widened view",
                            source.origin
                        );
                    }
                    const auto source_place = location(*value.source);
                    if (source_place.has_value()) {
                        flow = place(*value.source, std::move(*flow.normal));
                    } else {
                        aggregate(*value.source);
                    }
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    if (const auto* function = std::get_if<SemCallable>(&value.source->value)) {
                        flow.value = {
                            .loans = {{{}, std::nullopt, function->callable, source.origin, false}},
                            .captures = {}
                        };
                    } else {
                        const auto backing = source_place.has_value()
                            ? *source_place
                            : Place {temporaries.at(std::addressof(*value.source)), {}};
                        flow.value = {
                            .loans =
                                {{{},
                                  backing,
                                  callable_for(value.source->type),
                                  source.origin,
                                  !source_place.has_value()
                                      && type_contents.contents(value.source->type).closure_owner}},
                            .captures = {}
                        };
                    }
                },
                [&](const SemTake<TypeID, FailureSetID>& value) noexcept {
                    flow = place(*value.place, std::move(*flow.normal));
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    const auto target = *location(*value.place);
                    for (const auto& holder : flow.normal->objects) {
                        for (const auto& loan : holder.relationships.loans) {
                            if (loan.backing.has_value() && overlaps(*loan.backing, target)) {
                                diagnose(
                                    DiagnosticCode::AccessBorrowConflict,
                                    "Take conflicts with a live callable view",
                                    source.origin,
                                    loan.origin
                                );
                            }
                        }
                    }
                    for (const auto& access : accesses) {
                        if (overlaps(access.place, target)) {
                            diagnose(
                                DiagnosticCode::AccessOperationConflict,
                                "Take conflicts with an active access",
                                source.origin
                            );
                        }
                    }
                    for (const auto& holder : flow.normal->objects) {
                        for (const auto& capture : references(holder.relationships, *flow.normal)) {
                            if (capture.target.object == target.object) {
                                diagnose(
                                    DiagnosticCode::AccessCaptureConflict,
                                    "Take conflicts with a live Write capture",
                                    source.origin,
                                    capture.origin
                                );
                            }
                        }
                    }
                    auto& owner = flow.normal->objects[target.object];
                    flow.value = owner.relationships;
                    owner.available = false;
                    if (!owner.taken.has_value()) {
                        owner.taken = source.origin;
                    }
                    owner.relationships = {};
                },
                [&](const SemClosure<TypeID, FailureSetID>& value) noexcept {
                    for (const auto [index, capture] : std::views::enumerate(value.captures)) {
                        if (!flow.normal.has_value()) {
                            break;
                        }
                        if (capture.mode == CaptureMode::Write) {
                            auto selected = place(capture.expression, std::move(*flow.normal));
                            flow.normal = std::move(selected.normal);
                            append_exits(flow, selected);
                            if (!flow.normal.has_value()) {
                                break;
                            }
                            const auto target = *location(capture.expression);
                            write_access(target, capture.expression.origin);
                            flow.value.captures.push_back(
                                {ProjectionPath {index}, target, source.origin}
                            );
                        } else {
                            aggregate(capture.expression, ProjectionPath {index});
                        }
                    }
                },
                [&](const SemCpp<TypeID, FailureSetID>& value) noexcept {
                    if (type_contents.contains_view(source.type)) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "an undeclared C++ contract cannot establish a Carven callable borrow",
                            source.origin
                        );
                    }
                    const auto previous = accesses.size();
                    for (const auto& operand : value.operands) {
                        const auto relationships = evaluate(operand.expression, true);
                        if (!flow.normal.has_value()) {
                            break;
                        }
                        if (!relationships.loans.empty() || !relationships.captures.empty()) {
                            diagnose(
                                DiagnosticCode::TypeCallableViewEscape,
                                "tracked borrows cannot cross an undeclared C++ contract",
                                source.origin
                            );
                        }
                        if (operand.access != AccessMode::Take) {
                            if (const auto target = location(operand.expression)) {
                                if (operand.access == AccessMode::Write) {
                                    write_access(*target, operand.expression.origin);
                                }
                                accesses.push_back({*target, false});
                            }
                        }
                    }
                    accesses.resize(previous);
                },
                [&](const SemCall<TypeID, FailureSetID>& value) noexcept {
                    const auto previous = accesses.size();
                    const auto concrete = callable_for(value.callee->type);
                    const auto selected = location(*value.callee);
                    auto callee = Relationships {};
                    if (concrete.has_value() && selected.has_value()) {
                        auto located = place(*value.callee, std::move(*flow.normal));
                        flow.normal = std::move(located.normal);
                        append_exits(flow, located);
                        callee = std::move(located.value);
                    } else {
                        callee = evaluate(*value.callee);
                    }
                    if (selected.has_value() && concrete.has_value()) {
                        accesses.push_back({*selected, false});
                    }
                    const auto protect = [&](const Relationships& relationships) noexcept {
                        for (const auto& loan : relationships.loans) {
                            if (loan.backing.has_value()) {
                                accesses.push_back({*loan.backing, false});
                            }
                        }
                    };
                    protect(callee);
                    auto parameters = std::vector<CallArgument>();
                    for (const auto& argument : value.arguments) {
                        auto relationships = evaluate(argument.expression, true);
                        if (!flow.normal.has_value()) {
                            break;
                        }
                        auto alias = argument.access == AccessMode::Take
                            ? std::nullopt
                            : location(argument.expression);
                        if (alias.has_value()) {
                            if (argument.access == AccessMode::Write) {
                                write_access(*alias, argument.expression.origin);
                            }
                            accesses.push_back({*alias, false});
                        }
                        protect(relationships);
                        parameters.push_back({std::move(alias), std::move(relationships)});
                    }
                    if (flow.normal.has_value()) {
                        auto invoked = Flow {};
                        const auto invoke =
                            [&](this const auto& self,
                                const Relationships& target,
                                std::optional<CallableID> function) noexcept -> void {
                            use(target, *flow.normal, source.origin, true);
                            if (function.has_value()) {
                                auto next = call(
                                    *function,
                                    target,
                                    parameters,
                                    *flow.normal,
                                    source.origin
                                );
                                join_normal(invoked.normal, next.normal);
                                merge_relationships(invoked.value, next.value);
                                append_exits(invoked, next);
                                return;
                            }
                            for (const auto& loan : target.loans) {
                                if (loan.backing.has_value()) {
                                    const auto backing = project(
                                        flow.normal->objects[loan.backing->object].relationships,
                                        loan.backing->path
                                    );
                                    self(backing, loan.callable);
                                } else if (loan.callable.has_value()) {
                                    self({}, loan.callable);
                                } else {
                                    join_normal(invoked.normal, flow.normal);
                                    for (const auto type :
                                         draft.failure_set_copy(value.callee_failures).members) {
                                        invoked.exits.push_back(
                                            {ExitKind::Failure, type, *flow.normal, {}}
                                        );
                                    }
                                }
                            }
                        };
                        if (concrete.has_value() && selected.has_value()) {
                            callee = project(
                                flow.normal->objects[selected->object].relationships,
                                selected->path
                            );
                        }
                        if (std::holds_alternative<SemEnumConstructor>(value.callee->value)) {
                            invoked.normal = flow.normal;
                            for (const auto [index, parameter] :
                                 std::views::enumerate(parameters)) {
                                merge_relationships(
                                    invoked.value,
                                    nested(parameter.value, ProjectionPath {index})
                                );
                            }
                        } else {
                            invoke(callee, concrete);
                        }
                        flow.normal = std::move(invoked.normal);
                        flow.value = std::move(invoked.value);
                        append_exits(flow, invoked);
                    }
                    accesses.resize(previous);
                },
                [&](const SemPropagate<TypeID, FailureSetID>& value) noexcept {
                    flow.value = evaluate(*value.operand, direct);
                },
                [&](const SemIf<TypeID, FailureSetID>& value) noexcept {
                    flow = conditional(value, std::move(*flow.normal));
                },
                [&](const SemMatch<TypeID, FailureSetID>& value) noexcept {
                    flow = match(value, std::move(*flow.normal));
                },
                [&](const SemTry<TypeID, FailureSetID>& value) noexcept {
                    flow = attempt(value, std::move(*flow.normal));
                },
            },
            source.value
        );
    }
    if (flow.normal.has_value()) {
        use(flow.value, *flow.normal, source.origin, direct);
        if (source.category == SemanticValueCategory::Value) {
            retain(*flow.normal, flow.value, source);
        }
    }
    return flow;
}

} // namespace ownership
