module carven:semantic.analysis.ownership.expr.impl;

import :semantic.analysis.ownership.context;
import std;

auto OwnershipBodyAnalyzer::place(
    const SemanticExpression& source,
    OwnershipState state,
    bool read
) noexcept -> OwnershipFlow {
    auto result = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}, {}}, .exits = {}};
    if (const auto* dereference = std::get_if<SemDereference>(&source.value)) {
        auto next = expression(*dereference->source, std::move(result.normal->state));
        result.normal = std::move(next.normal);
        append_ownership_exits(result, next);
        if (result.normal) {
            result.normal->storage.clear();
        }
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
        if (result.normal) {
            for (auto& selected : result.normal->storage) {
                selected.path.push_back(field->field.field_index);
            }
        }
    } else if (const auto* index = std::get_if<SemIndex>(&source.value)) {
        result = place(*index->source, std::move(result.normal->state));
        if (result.normal.has_value()) {
            const auto previous = accesses.size();
            if (const auto target = location(*index->source)) {
                accesses.push_back({*target, false});
            }
            auto storage = std::move(result.normal->storage);
            for (auto& selected : storage) {
                selected.path.push_back(constant_index(*index->index));
            }
            auto indexed = expression(*index->index, std::move(result.normal->state));
            accesses.resize(previous);
            result.normal = std::move(indexed.normal);
            if (result.normal) {
                result.normal->storage = std::move(storage);
            }
            append_ownership_exits(result, indexed);
        }
    }
    if (result.normal.has_value()) {
        if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
            const auto found = read_storage.find(binding->binding);
            result.normal->storage = found != read_storage.end()
                ? found->second
                : std::vector {binding_place(binding->binding)};
        }
        if (result.normal->storage.empty()) {
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
        result.normal->value = {};
        for (const auto& target : result.normal->storage) {
            if (read) {
                require_available(result.normal->state, target, source.origin);
            }
            merge_relationships(
                result.normal->value,
                project_relationships(
                    result.normal->state.objects[target.object].relationships,
                    target.path
                )
            );
        }
        if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
            const auto* parameter =
                std::get_if<ParameterBindingStorage>(&body.binding(binding->binding).storage);
            const auto* capture =
                std::get_if<CaptureBindingStorage>(&body.binding(binding->binding).storage);
            if (((parameter != nullptr && parameter->access == AccessMode::Read)
                 || (capture != nullptr && capture->mode != CaptureMode::Write))
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
    auto flow = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}, {}}, .exits = {}};
    auto operand_storage = std::vector<OwnershipPlace>();
    const auto evaluate = [&](const SemanticExpression& child,
                              bool argument = false) noexcept -> OwnershipRelationships {
        if (!flow.normal.has_value()) {
            return {};
        }
        auto accumulated = std::move(flow.normal->value);
        auto storage = std::move(flow.normal->storage);
        const auto previous_readers = storage_readers.size();
        protect_storage(accumulated);
        auto next = expression(child, std::move(flow.normal->state), argument);
        restore_storage_readers(previous_readers);
        auto value = next.normal ? std::move(next.normal->value) : OwnershipRelationships {};
        operand_storage =
            next.normal ? std::move(next.normal->storage) : std::vector<OwnershipPlace> {};
        flow.normal = std::move(next.normal);
        if (flow.normal) {
            flow.normal->value = std::move(accumulated);
            flow.normal->storage = std::move(storage);
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
            const auto previous_readers = storage_readers.size();
            auto writes = std::vector<OwnershipPlace>();
            visit_cpp_operands(
                value,
                [&](AccessMode access, const SemanticExpression& operand) noexcept {
                    const auto relationships = evaluate(operand, true);
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    if (!relationships.callable_loans.empty() || !relationships.captures.empty()) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "tracked borrows cannot cross an undeclared C++ contract",
                            source.origin
                        );
                    }
                    protect_storage(relationships);
                    const auto snapshot = access == AccessMode::Read
                        && std::holds_alternative<PointerTypeValue>(
                                              program.types().type(operand.type.resolved()).value
                        );
                    if (access != AccessMode::Take && !snapshot) {
                        if (const auto target = location(operand)) {
                            if (access == AccessMode::Write) {
                                write_access(*target, operand.origin);
                                writes.push_back(*target);
                            }
                            accesses.push_back({*target, false});
                        }
                    }
                }
            );
            if (flow.normal) {
                // Native Write may leave the old view in place; keep its known loans.
                for (const auto& target : writes) {
                    check_storage_write(flow.normal->state, target, source.origin);
                }
                // Native results obey the provider/caller storage contract.
                flow.normal->value = {};
            }
            restore_storage_readers(previous_readers);
            accesses.resize(previous);
        };
        std::visit(
            Overloaded {
                [](const SemConstant&) static noexcept {},
                [&](const SemBinding&) noexcept {
                    flow = place(source, std::move(flow.normal->state));
                },
                [&](const SemCallable& value) noexcept {
                    flow.normal->value.callable_loans.push_back(
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
                    const auto previous_readers = storage_readers.size();
                    protect_storage(evaluate(*value.left));
                    static_cast<void>(evaluate(*value.right));
                    restore_storage_readers(previous_readers);
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
                        flow.normal->storage = std::move(operand_storage);
                        for (auto& selected : flow.normal->storage) {
                            selected.path.push_back(value.field.field_index);
                        }
                    }
                },
                [&](const SemIndex& value) noexcept {
                    const auto previous_readers = storage_readers.size();
                    const auto relationships = evaluate(*value.source);
                    auto storage = select_element_storage(
                        program.types(),
                        value.source->type.resolved(),
                        operand_storage,
                        relationships,
                        constant_index(*value.index)
                    );
                    protect_storage(relationships);
                    static_cast<void>(evaluate(*value.index));
                    restore_storage_readers(previous_readers);
                    if (flow.normal) {
                        flow.normal->value = project_relationships(
                            relationships,
                            OwnershipProjectionPath {constant_index(*value.index)}
                        );
                        flow.normal->storage = std::move(storage);
                    }
                },
                [&](const SemTestReport& value) noexcept {
                    if (value.condition.has_value()) {
                        evaluate(**value.condition);
                    }
                    if (value.message.has_value()) {
                        evaluate(**value.message);
                    }
                    const auto known = value.condition.has_value()
                        ? constant_truth(**value.condition)
                        : std::optional(false);
                    if (flow.normal && value.kind != TestReportKind::Check && known != true) {
                        flow.exits.push_back({OwnershipTestStopped {}, flow.normal->state});
                    }
                    if (value.kind == TestReportKind::Fail
                        || (value.kind == TestReportKind::Require && known == false)) {
                        flow.normal.reset();
                    }
                },
                [&]<typename Output>(const Output& value) noexcept
                    requires (std::same_as<Output, SemPrint> || std::same_as<Output, SemFormat>)
                {
                    const auto previous_accesses = accesses.size();
                    const auto previous_readers = storage_readers.size();
                    for (const auto& operand : value.operands) {
                        const auto relationships = evaluate(operand.expression, true);
                        if (!flow.normal) {
                            break;
                        }
                        if (!relationships.callable_loans.empty()
                            || !relationships.captures.empty()) {
                            diagnose(
                                DiagnosticCode::TypeCallableViewEscape,
                                "tracked callable borrows cannot cross a C++ formatter contract",
                                source.origin
                            );
                        }
                        protect_storage(relationships);
                        if (!std::holds_alternative<PointerTypeValue>(
                                program.types().type(operand.expression.type.resolved()).value
                            )) {
                            if (const auto target = location(operand.expression)) {
                                accesses.push_back({*target, false});
                            }
                        }
                    }
                    if (flow.normal) {
                        flow.normal->value = {};
                    }
                    restore_storage_readers(previous_readers);
                    accesses.resize(previous_accesses);
                },
                [&](const SemSliceIntrinsic& value) noexcept {
                    const auto previous_readers = storage_readers.size();
                    auto relationships = evaluate(value.operands.front().expression);
                    if (value.intrinsic == SliceIntrinsic::FromArray
                        || value.intrinsic == SliceIntrinsic::Slice) {
                        // A subslice rebases indices. Keep every element's nested borrows
                        // reachable through an unknown element index.
                        const auto rebase = [](auto& rows) static noexcept {
                            for (auto& row : rows) {
                                if (!row.holder.empty()) {
                                    row.holder.front() = std::nullopt;
                                }
                            }
                        };
                        rebase(relationships.callable_loans);
                        rebase(relationships.captures);
                        rebase(relationships.storage_loans);
                        normalize_relationships(relationships);
                    }
                    if (value.intrinsic == SliceIntrinsic::FromArray) {
                        for (const auto& backing : operand_storage) {
                            relationships.storage_loans.push_back({{}, backing, source.origin});
                        }
                    }
                    protect_storage(relationships);
                    for (auto i = 1uz; i < value.operands.size(); ++i) {
                        static_cast<void>(evaluate(value.operands[i].expression));
                    }
                    if (flow.normal) {
                        flow.normal->value = value.intrinsic == SliceIntrinsic::FromArray
                                || value.intrinsic == SliceIntrinsic::Slice
                            ? std::move(relationships)
                            : OwnershipRelationships {};
                    }
                    restore_storage_readers(previous_readers);
                },
                [&](const SemTextIntrinsic& value) noexcept {
                    const auto previous_accesses = accesses.size();
                    const auto previous_readers = storage_readers.size();
                    auto borrowed = OwnershipRelationships {};
                    for (const auto& [index, operand] : std::views::enumerate(value.operands)) {
                        auto relationships = evaluate(operand.expression);
                        if (!flow.normal) {
                            break;
                        }
                        if (index == 0
                            && (value.intrinsic == TextIntrinsic::FromUTF8Unchecked
                                || value.intrinsic == TextIntrinsic::AsStr
                                || value.intrinsic == TextIntrinsic::Bytes
                                || value.intrinsic == TextIntrinsic::Chars)) {
                            if (program.types().type(operand.expression.type.resolved()).value
                                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}) {
                                for (const auto& backing : operand_storage) {
                                    relationships.storage_loans.push_back(
                                        {{}, backing, source.origin}
                                    );
                                }
                            }
                            borrowed = relationships;
                        }
                        protect_storage(relationships);
                        if (index == 0) {
                            if (const auto selected = location(operand.expression)) {
                                accesses.push_back({*selected, false});
                            }
                        }
                    }
                    if (flow.normal) {
                        if (text_intrinsic_writes(value.intrinsic)) {
                            if (const auto target = location(value.operands.front().expression)) {
                                write_access(*target, source.origin);
                                check_storage_write(flow.normal->state, *target, source.origin);
                            }
                        }
                        flow.normal->value = std::move(borrowed);
                    }
                    restore_storage_readers(previous_readers);
                    accesses.resize(previous_accesses);
                },
                [&](const SemArrayAdopt& value) noexcept {
                    const auto original = evaluate(*value.source);
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    const auto& storage = operand_storage;
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
                            flow.normal->value.callable_loans.push_back(
                                {path, std::nullopt, *callable_id, source.origin, false}
                            );
                            return;
                        }
                        for (auto element : storage) {
                            element.path.insert(element.path.end(), path.begin(), path.end());
                            flow.normal->value.callable_loans.push_back(
                                {path,
                                 element,
                                 callable_for(from),
                                 source.origin,
                                 full_expression_storage(element.object)}
                            );
                        }
                    };
                    flow.normal->value.storage_loans = original.storage_loans;
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
                    const auto relationships = evaluate(*value.source);
                    const auto& backing = operand_storage;
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    flow.normal->value = relationships;
                    // Equal view types copy the target description. They do not
                    // borrow the intermediate view's storage.
                    if (value.source->type.resolved() == source.type.resolved()) {
                        return;
                    }
                    auto storage = std::move(flow.normal->value.storage_loans);
                    if (const auto callable_id =
                            stateless_callable_for(value.source->type.resolved())) {
                        flow.normal->value = {
                            .callable_loans =
                                {{{}, std::nullopt, *callable_id, source.origin, false}},
                            .captures = {},
                            .storage_loans = std::move(storage),
                        };
                    } else {
                        flow.normal->value = {
                            .callable_loans = {},
                            .captures = {},
                            .storage_loans = std::move(storage),
                        };
                        for (const auto& selected : backing) {
                            flow.normal->value.callable_loans.push_back(
                                {{},
                                 selected,
                                 callable_for(value.source->type.resolved()),
                                 source.origin,
                                 full_expression_storage(selected.object)
                                     && analysis.contents(value.source->type.resolved())
                                            .closure_owner}
                            );
                        }
                    }
                },
                [&](const SemTake& value) noexcept {
                    flow = place(*value.place, std::move(flow.normal->state));
                    if (!flow.normal.has_value()) {
                        return;
                    }
                    const auto target = *location(*value.place);
                    check_storage_write(flow.normal->state, target, source.origin);
                    for (const auto& holder : flow.normal->state.objects) {
                        for (const auto& loan : holder.relationships.callable_loans) {
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
                    const auto previous_readers = storage_readers.size();
                    const auto concrete = callable_for(value.callee->type.resolved());
                    const auto selected = location(*value.callee);
                    auto callee = OwnershipRelationships {};
                    auto callee_storage = std::vector<OwnershipPlace>();
                    if (concrete.has_value() && selected.has_value()) {
                        auto located = place(*value.callee, std::move(flow.normal->state));
                        flow.normal = std::move(located.normal);
                        append_ownership_exits(flow, located);
                        callee =
                            flow.normal ? std::move(flow.normal->value) : OwnershipRelationships {};
                        if (flow.normal) {
                            callee_storage = flow.normal->storage;
                        }
                    } else {
                        callee = evaluate(*value.callee);
                        callee_storage = operand_storage;
                    }
                    if (selected.has_value() && concrete.has_value()) {
                        accesses.push_back({*selected, false});
                    }
                    const auto protect = [&](const OwnershipRelationships& relationships,
                                             bool storage) noexcept {
                        if (storage) {
                            protect_storage(relationships);
                        }
                        for (const auto& loan : relationships.callable_loans) {
                            if (loan.backing.has_value()) {
                                accesses.push_back({*loan.backing, false});
                            }
                        }
                    };
                    protect(callee, true);
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
                        auto storage = std::vector<OwnershipPlace>();
                        if (argument.access == AccessMode::Read
                            && analysis.contents(argument.expression.type.resolved())
                                   .read_borrows_storage()) {
                            storage = operand_storage;
                            alias.reset();
                            for (const auto& selected : storage) {
                                accesses.push_back({selected, false});
                            }
                        }
                        if (alias.has_value()) {
                            if (argument.access == AccessMode::Write) {
                                write_access(*alias, argument.expression.origin);
                            }
                            accesses.push_back({*alias, false});
                        }
                        protect(relationships, argument.access != AccessMode::Write);
                        parameters.push_back(
                            {std::move(alias), std::move(relationships), std::move(storage)}
                        );
                    }
                    if (flow.normal.has_value()) {
                        auto invoked = OwnershipFlow {};
                        const auto invoke =
                            [&](this const auto& self,
                                const OwnershipRelationships& target,
                                std::optional<CallableID> function,
                                std::optional<OwnershipPlace> capture_owner) noexcept -> void {
                            use(target, flow.normal->state, source.origin, true);
                            if (function.has_value()) {
                                auto next = call(
                                    *function,
                                    target,
                                    std::move(capture_owner),
                                    parameters,
                                    flow.normal->state,
                                    source.origin
                                );
                                join_normal_ownership(invoked.normal, next.normal);

                                append_ownership_exits(invoked, next);
                                return;
                            }
                            for (const auto& loan : target.callable_loans) {
                                if (loan.backing.has_value()) {
                                    if (!flow.normal->state.objects[loan.backing->object]
                                             .available) {
                                        // use() diagnoses the expired backing. There is
                                        // no live callable state to interpret here.
                                        continue;
                                    }
                                    auto backing = project_relationships(
                                        flow.normal->state.objects[loan.backing->object]
                                            .relationships,
                                        loan.backing->path
                                    );
                                    merge_relationships(
                                        backing,
                                        {
                                            .callable_loans = {},
                                            .captures = {},
                                            .storage_loans = target.storage_loans,
                                        }
                                    );
                                    self(backing, loan.callable, loan.backing);
                                } else if (loan.callable.has_value()) {
                                    self({}, loan.callable, std::nullopt);
                                } else {
                                    join_normal_ownership(invoked.normal, flow.normal);
                                    for (const auto type :
                                         program.failure_sets()
                                             .failure_set(value.callee_failures.resolved())
                                             .members) {
                                        invoked.exits.push_back(
                                            {OwnershipFailure {type, {}}, flow.normal->state}
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
                        } else if (concrete && !callee_storage.empty()) {
                            for (const auto& backing : callee_storage) {
                                invoke(
                                    project_relationships(
                                        flow.normal->state.objects[backing.object].relationships,
                                        backing.path
                                    ),
                                    concrete,
                                    backing
                                );
                            }
                        } else {
                            invoke(callee, concrete, std::nullopt);
                        }
                        flow.normal = std::move(invoked.normal);

                        append_ownership_exits(flow, invoked);
                    }
                    accesses.resize(previous);
                    restore_storage_readers(previous_readers);
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
        if (!source.selects_storage()) {
            flow.normal->storage.clear();
            if (const auto found = facts.temporaries.find(std::addressof(source));
                found != facts.temporaries.end()) {
                flow.normal->storage.push_back({input.objects.size() + found->second, {}});
            }
        }
        use(flow.normal->value, flow.normal->state, source.origin, direct);
        if (source.category == SemanticValueCategory::Value) {
            retain(flow.normal->state, flow.normal->value, source);
        }
    }
    return flow;
}
