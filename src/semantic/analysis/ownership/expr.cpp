module carven:semantic.analysis.ownership.expr.impl;

import :semantic.analysis.ownership.context;
import std;

auto OwnershipBodyAnalyzer::place(
    const SemanticExpression& source,
    OwnershipState state,
    bool read
) noexcept -> OwnershipFlow {
    auto result = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}}, .exits = {}};
    if (const auto* dereference = std::get_if<SemDereference>(&source.value)) {
        auto next = expression(*dereference->source, std::move(result.normal->state));
        result.normal = std::move(next.normal);
        append_ownership_exits(result, next);
    } else if (const auto* foreign = std::get_if<SemCpp>(&source.value)) {
        result = place(foreign->operands.front().expression, std::move(result.normal->state));
        for (const auto& operand : std::span(foreign->operands).subspan(1)) {
            if (!result.normal.has_value()) {
                break;
            }
            const auto previous = accesses.size();
            if (const auto target = location(foreign->operands.front().expression)) {
                accesses.push_back({*target, false});
            }
            auto next = expression(operand.expression, std::move(result.normal->state));
            accesses.resize(previous);
            result.normal = std::move(next.normal);
            append_ownership_exits(result, next);
        }
    } else if (const auto* field = std::get_if<SemField>(&source.value)) {
        result = place(*field->source, std::move(result.normal->state));
    } else if (const auto* index = std::get_if<SemIndex>(&source.value)) {
        result = place(*index->source, std::move(result.normal->state));
        if (result.normal.has_value()) {
            const auto previous = accesses.size();
            if (const auto target = location(*index->source)) {
                accesses.push_back({*target, false});
            }
            auto constant_index = expression(*index->index, std::move(result.normal->state));
            accesses.resize(previous);
            result.normal = std::move(constant_index.normal);
            append_ownership_exits(result, constant_index);
        }
    }
    if (result.normal.has_value()) {
        const auto target = location(source);
        if (!target.has_value()) {
            result.normal->value = {};
            if (source.category == SemanticValueCategory::Value
                && analysis.contents(source.type.resolved()).callable_view) {
                diagnose(
                    DiagnosticCode::TypeCallableViewEscape,
                    "an indirect target cannot establish a Carven callable borrow",
                    source.origin
                );
            }
            return result;
        }
        if (read) {
            require_available(result.normal->state, *target, source.origin);
        }
        result.normal->value = project_relationships(
            result.normal->state.objects[target->object].relationships,
            target->path
        );
        if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
            const auto* parameter =
                std::get_if<ParameterBindingStorage>(&body.binding(binding->binding).storage);
            if (parameter != nullptr
                && parameter->access == AccessMode::Read
                && aliases.contains(binding->binding)) {
                merge_relationships(
                    result.normal->value,
                    result.normal->state.objects[input.objects.size() + binding->binding.index()]
                        .relationships
                );
            }
        }
    }
    return result;
}

auto OwnershipBodyAnalyzer::expression(
    const SemanticExpression& source,
    OwnershipState state,
    bool direct
) noexcept -> OwnershipFlow {
    auto flow = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}}, .exits = {}};
    const auto evaluate = [&](const SemanticExpression& child,
                              bool argument = false) noexcept -> OwnershipRelationships {
        if (!flow.normal.has_value()) {
            return {};
        }
        auto accumulated = std::move(flow.normal->value);
        auto next = expression(child, std::move(flow.normal->state), argument);
        auto value = next.normal ? std::move(next.normal->value) : OwnershipRelationships {};
        flow.normal = std::move(next.normal);
        if (flow.normal) {
            flow.normal->value = std::move(accumulated);
        }
        append_ownership_exits(flow, next);
        return value;
    };
    const auto aggregate = [&](const SemanticExpression& child,
                               const OwnershipProjectionPath& path = {}) noexcept {
        auto value = evaluate(child);
        if (flow.normal) {
            merge_relationships(flow.normal->value, nest_relationships(std::move(value), path));
        }
    };
    const auto callable_for = [&](TypeID type) noexcept -> std::optional<CallableID> {
        const auto value = program.types().type(type).value;
        if (const auto* closure = std::get_if<ClosureTypeValue>(&value)) {
            return closure->callable;
        }
        if (const auto* function = std::get_if<FunctionTypeValue>(&value)) {
            return function->callable;
        }
        return std::nullopt;
    };
    const auto stateless_callable_for = [&](TypeID type) noexcept -> std::optional<CallableID> {
        const auto callable_id = callable_for(type);
        if (!callable_id.has_value()) {
            return std::nullopt;
        }
        const auto body_id = program.declarations().body_for_callable(*callable_id);
        return !body_id.has_value() || program.bodies().body(*body_id).inputs().captures.empty()
            ? callable_id
            : std::nullopt;
    };
    if (source.category == SemanticValueCategory::Place) {
        flow = place(source, std::move(flow.normal->state));
    } else {
        const auto external = [&](const auto& value) noexcept {
            if (analysis.contents(source.type.resolved()).callable_view) {
                diagnose(
                    DiagnosticCode::TypeCallableViewEscape,
                    "an undeclared C++ contract cannot establish a Carven callable borrow",
                    source.origin
                );
            }
            const auto previous = accesses.size();
            visit_cpp_operands(
                value,
                [&](AccessMode access, const SemanticExpression& operand) noexcept {
                    const auto relationships = evaluate(operand, true);
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    if (!relationships.loans.empty() || !relationships.captures.empty()) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "tracked borrows cannot cross an undeclared C++ contract",
                            source.origin
                        );
                    }
                    const auto snapshot = access == AccessMode::Read
                        && std::holds_alternative<PointerTypeValue>(
                                              program.types().type(operand.type.resolved()).value
                        );
                    if (access != AccessMode::Take && !snapshot) {
                        if (const auto target = location(operand)) {
                            if (access == AccessMode::Write) {
                                write_access(*target, operand.origin);
                            }
                            accesses.push_back({*target, false});
                        }
                    }
                }
            );
            accesses.resize(previous);
        };
        std::visit(
            Overloaded {
                [](const SemConstant&) static noexcept {},
                [&](const SemBinding&) noexcept {
                    flow = place(source, std::move(flow.normal->state));
                },
                [&](const SemCallable& value) noexcept {
                    flow.normal->value.loans.push_back(
                        {{}, std::nullopt, value.callable, source.origin, false}
                    );
                },
                [](const SemEnumConstructor&) static noexcept {},
                [&](const SemArray& value) noexcept {
                    for (const auto [index, child] : std::views::enumerate(value.elements)) {
                        aggregate(child, OwnershipProjectionPath {index});
                    }
                },
                [&](const SemStruct& value) noexcept {
                    for (const auto& field : value.fields) {
                        aggregate(field.value, OwnershipProjectionPath {field.declaration_index});
                    }
                },
                [&](const SemEnumCase& value) noexcept {
                    for (const auto [index, child] : std::views::enumerate(value.payload)) {
                        aggregate(child, OwnershipProjectionPath {index});
                    }
                },
                [&](const SemUnary& value) noexcept {
                    static_cast<void>(evaluate(*value.operand));
                },
                [&](const SemBinary& value) noexcept {
                    static_cast<void>(evaluate(*value.left));
                    static_cast<void>(evaluate(*value.right));
                },
                [&](const SemCast& value) noexcept { aggregate(*value.operand); },
                [&](const SemShortCircuit& value) noexcept {
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
                        join_normal_ownership(flow.normal, skipped);
                    }
                },
                [&](const SemDereference&) noexcept {
                    flow = place(source, std::move(flow.normal->state));
                },
                [&](const SemField& value) noexcept {
                    const auto relationships = evaluate(*value.source);
                    if (flow.normal) {
                        flow.normal->value = project_relationships(
                            relationships,
                            OwnershipProjectionPath {value.field.field_index}
                        );
                    }
                },
                [&](const SemIndex& value) noexcept {
                    const auto relationships = evaluate(*value.source);
                    static_cast<void>(evaluate(*value.index));
                    if (flow.normal) {
                        flow.normal->value = project_relationships(
                            relationships,
                            OwnershipProjectionPath {constant_index(*value.index)}
                        );
                    }
                },
                [&](const SemTextIntrinsic& value) noexcept {
                    static_cast<void>(evaluate(*value.source));
                },
                [&](const SemArrayAdopt& value) noexcept {
                    const auto original = evaluate(*value.source);
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    const auto source_place = location(*value.source);
                    const auto backing = source_place.has_value()
                        ? *source_place
                        : OwnershipPlace {temporary(*value.source), {}};
                    const auto adopt = [&](this const auto& self,
                                           TypeID from,
                                           TypeID to,
                                           const OwnershipProjectionPath& path) noexcept -> void {
                        if (from == to) {
                            merge_relationships(
                                flow.normal->value,
                                nest_relationships(project_relationships(original, path), path)
                            );
                            return;
                        }
                        const auto target = program.types().type(to).value;
                        if (const auto* array = std::get_if<ArrayTypeValue>(&target)) {
                            const auto input =
                                std::get<ArrayTypeValue>(program.types().type(from).value);
                            for (auto index = 0uz; index < array->extent; ++index) {
                                auto element = path;
                                element.push_back(index);
                                self(input.element, array->element, element);
                            }
                            return;
                        }
                        if (const auto callable_id = stateless_callable_for(from)) {
                            flow.normal->value.loans.push_back(
                                {path, std::nullopt, *callable_id, source.origin, false}
                            );
                            return;
                        }
                        auto element = backing;
                        element.path.insert(element.path.end(), path.begin(), path.end());
                        flow.normal->value.loans.push_back(
                            {path,
                             std::move(element),
                             callable_for(from),
                             source.origin,
                             !source_place.has_value()}
                        );
                    };
                    adopt(value.source->type.resolved(), source.type.resolved(), {});
                },
                [&](const SemBorrowCallable& value) noexcept {
                    if (std::holds_alternative<SemTake>(value.source->value)
                        && analysis.contents(value.source->type.resolved()).callable_view
                        && value.source->type.resolved() != source.type.resolved()) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "taken callable storage cannot back a widened view",
                            source.origin
                        );
                    }
                    const auto source_place = location(*value.source);
                    if (source_place.has_value()) {
                        flow = place(*value.source, std::move(flow.normal->state));
                    } else {
                        aggregate(*value.source);
                    }
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    // Equal view types copy the target description. They do not
                    // borrow the intermediate view's storage.
                    if (value.source->type.resolved() == source.type.resolved()) {
                        return;
                    }
                    if (const auto callable_id =
                            stateless_callable_for(value.source->type.resolved())) {
                        flow.normal->value = {
                            .loans = {{{}, std::nullopt, *callable_id, source.origin, false}},
                            .captures = {},
                        };
                    } else {
                        const auto backing = source_place.has_value()
                            ? *source_place
                            : OwnershipPlace {temporary(*value.source), {}};
                        flow.normal->value = {
                            .loans =
                                {{{},
                                  backing,
                                  callable_for(value.source->type.resolved()),
                                  source.origin,
                                  !source_place.has_value()
                                      && analysis.contents(value.source->type.resolved())
                                             .closure_owner}},
                            .captures = {}
                        };
                    }
                },
                [&](const SemTake& value) noexcept {
                    flow = place(*value.place, std::move(flow.normal->state));
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    const auto target = *location(*value.place);
                    for (const auto& holder : flow.normal->state.objects) {
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
                    for (const auto& holder : flow.normal->state.objects) {
                        for (const auto& capture :
                             references(holder.relationships, flow.normal->state)) {
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
                    auto& owner = flow.normal->state.objects[target.object];
                    flow.normal->value = owner.relationships;
                    owner.available = false;
                    if (!owner.taken.has_value()) {
                        owner.taken = source.origin;
                    }
                    owner.relationships = {};
                },
                [&](const SemClosure& value) noexcept {
                    for (const auto [index, capture] : std::views::enumerate(value.captures)) {
                        if (!flow.normal.has_value()) {
                            break;
                        }
                        if (capture.mode == CaptureMode::Write) {
                            auto accumulated = std::move(flow.normal->value);
                            auto selected =
                                place(capture.expression, std::move(flow.normal->state));
                            flow.normal = std::move(selected.normal);
                            append_ownership_exits(flow, selected);
                            if (!flow.normal.has_value()) {
                                break;
                            }
                            flow.normal->value = std::move(accumulated);
                            const auto target = *location(capture.expression);
                            write_access(target, capture.expression.origin);
                            flow.normal->value.captures.push_back(
                                {OwnershipProjectionPath {index}, target, source.origin}
                            );
                        } else {
                            aggregate(capture.expression, OwnershipProjectionPath {index});
                        }
                    }
                },
                [&](const SemCpp& value) noexcept { external(value); },
                [&](const SemCppCall& value) noexcept { external(value); },
                [&](const SemCall& value) noexcept {
                    const auto previous = accesses.size();
                    const auto concrete = callable_for(value.callee->type.resolved());
                    const auto selected = location(*value.callee);
                    auto callee = OwnershipRelationships {};
                    if (concrete.has_value() && selected.has_value()) {
                        auto located = place(*value.callee, std::move(flow.normal->state));
                        flow.normal = std::move(located.normal);
                        append_ownership_exits(flow, located);
                        callee =
                            flow.normal ? std::move(flow.normal->value) : OwnershipRelationships {};
                    } else {
                        callee = evaluate(*value.callee);
                    }
                    if (selected.has_value() && concrete.has_value()) {
                        accesses.push_back({*selected, false});
                    }
                    const auto protect = [&](const OwnershipRelationships& relationships) noexcept {
                        for (const auto& loan : relationships.loans) {
                            if (loan.backing.has_value()) {
                                accesses.push_back({*loan.backing, false});
                            }
                        }
                    };
                    protect(callee);
                    auto parameters = std::vector<OwnershipCallArgument>();
                    for (const auto& argument : value.arguments) {
                        auto relationships = evaluate(argument.expression, true);
                        if (!flow.normal.has_value()) {
                            break;
                        }
                        const auto snapshot =
                            argument.access == AccessMode::Read
                            && std::holds_alternative<PointerTypeValue>(
                                program.types().type(argument.expression.type.resolved()).value
                            );
                        auto alias = argument.access == AccessMode::Take || snapshot
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
                        auto invoked = OwnershipFlow {};
                        const auto invoke =
                            [&](this const auto& self,
                                const OwnershipRelationships& target,
                                std::optional<CallableID> function) noexcept -> void {
                            use(target, flow.normal->state, source.origin, true);
                            if (function.has_value()) {
                                auto next = call(
                                    *function,
                                    target,
                                    parameters,
                                    flow.normal->state,
                                    source.origin
                                );
                                join_normal_ownership(invoked.normal, next.normal);

                                append_ownership_exits(invoked, next);
                                return;
                            }
                            for (const auto& loan : target.loans) {
                                if (loan.backing.has_value()) {
                                    if (!flow.normal->state.objects[loan.backing->object]
                                             .available) {
                                        // use() diagnoses the expired backing. There is
                                        // no live callable state to interpret here.
                                        continue;
                                    }
                                    const auto backing = project_relationships(
                                        flow.normal->state.objects[loan.backing->object]
                                            .relationships,
                                        loan.backing->path
                                    );
                                    self(backing, loan.callable);
                                } else if (loan.callable.has_value()) {
                                    self({}, loan.callable);
                                } else {
                                    join_normal_ownership(invoked.normal, flow.normal);
                                    for (const auto type :
                                         program.failure_sets()
                                             .failure_set(value.callee_failures.resolved())
                                             .members) {
                                        invoked.exits.push_back(
                                            {OwnershipFailure {type}, flow.normal->state}
                                        );
                                    }
                                }
                            }
                        };
                        if (concrete.has_value() && selected.has_value()) {
                            callee = project_relationships(
                                flow.normal->state.objects[selected->object].relationships,
                                selected->path
                            );
                        }
                        if (std::holds_alternative<SemEnumConstructor>(value.callee->value)) {
                            invoked.normal = flow.normal;
                            for (const auto [index, parameter] :
                                 std::views::enumerate(parameters)) {
                                merge_relationships(
                                    invoked.normal->value,
                                    nest_relationships(
                                        parameter.value,
                                        OwnershipProjectionPath {index}
                                    )
                                );
                            }
                        } else {
                            invoke(callee, concrete);
                        }
                        flow.normal = std::move(invoked.normal);

                        append_ownership_exits(flow, invoked);
                    }
                    accesses.resize(previous);
                },
                [&](const SemPropagate& value) noexcept {
                    auto propagated = evaluate(*value.operand, direct);
                    if (flow.normal) {
                        flow.normal->value = std::move(propagated);
                    }
                },
                [&](const SemIf& value) noexcept {
                    flow = conditional(value, std::move(flow.normal->state));
                },
                [&](const SemMatch& value) noexcept {
                    flow = match(value, std::move(flow.normal->state));
                },
                [&](const SemTry& value) noexcept {
                    flow = attempt(value, std::move(flow.normal->state));
                },
            },
            source.value
        );
    }
    if (flow.normal.has_value()) {
        use(flow.normal->value, flow.normal->state, source.origin, direct);
        if (source.category == SemanticValueCategory::Value) {
            retain(flow.normal->state, flow.normal->value, source);
        }
    }
    return flow;
}
