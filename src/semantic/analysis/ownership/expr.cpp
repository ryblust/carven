module carven:semantic.analysis.ownership.expr.impl;

import :semantic.analysis.ownership.context;
import std;

auto OwnershipBodyAnalyzer::place(
    const SemanticExpression& source,
    OwnershipState state,
    bool read
) noexcept -> ContinuationTask<OwnershipFlow> {
    auto result = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}, {}}, .exits = {}};
    if (const auto* dereference = std::get_if<SemDereference>(&source.value)) {
        auto next = (co_await expression(*dereference->source, std::move(result.normal->state)));
        result.normal = std::move(next.normal);
        append_ownership_exits(result, next);
        if (result.normal) {
            result.normal->storage.clear();
        }
    } else if (const auto* foreign = std::get_if<SemCpp>(&source.value)) {
        result =
            (co_await place(foreign->operands.front().expression, std::move(result.normal->state)));
        for (const auto& operand : std::span(foreign->operands).subspan(1)) {
            if (!result.normal.has_value()) {
                break;
            }
            const auto previous = accesses.size();
            auto storage = result.normal->storage;
            for (const auto& target : storage) {
                accesses.push_back({target, false});
            }
            auto next = (co_await expression(operand.expression, std::move(result.normal->state)));
            accesses.resize(previous);
            result.normal = std::move(next.normal);
            if (result.normal) {
                result.normal->storage = std::move(storage);
            }
            append_ownership_exits(result, next);
        }
    } else if (const auto* field = std::get_if<SemField>(&source.value)) {
        result = (co_await place(*field->source, std::move(result.normal->state)));
        if (result.normal) {
            for (auto& selected : result.normal->storage) {
                selected.path.push_back(field->field.field_index);
            }
        }
    } else if (const auto* index = std::get_if<SemIndex>(&source.value)) {
        result = (co_await place(*index->source, std::move(result.normal->state)));
        if (result.normal.has_value()) {
            const auto previous = accesses.size();
            for (const auto& target : result.normal->storage) {
                accesses.push_back({target, false});
            }
            auto storage = std::move(result.normal->storage);
            for (auto& selected : storage) {
                selected.path.push_back(constant_index(*index->index));
            }
            auto indexed = (co_await expression(*index->index, std::move(result.normal->state)));
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
            result.normal->storage = binding_places(binding->binding, result.normal->state);
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
            co_return result;
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
    co_return result;
}

auto OwnershipBodyAnalyzer::expression(
    const SemanticExpression& source,
    OwnershipState state,
    bool direct
) noexcept -> ContinuationTask<OwnershipFlow> {
    auto flow = OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}, {}}, .exits = {}};
    auto operand_storage = std::vector<OwnershipPlace>();
    const auto evaluate = [&](const SemanticExpression& child,
                              bool argument =
                                  false) noexcept -> ContinuationTask<OwnershipRelationships> {
        if (!flow.normal.has_value()) {
            co_return {};
        }
        auto accumulated = std::move(flow.normal->value);
        auto storage = std::move(flow.normal->storage);
        const auto previous_readers = storage_readers.size();
        protect_storage(accumulated);
        auto next = (co_await expression(child, std::move(flow.normal->state), argument));
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
        co_return value;
    };
    const auto aggregate =
        [&](const SemanticExpression& child,
            const OwnershipProjectionPath& path = {}) noexcept -> ContinuationTask<std::monostate> {
        auto value = (co_await evaluate(child));
        if (flow.normal) {
            merge_relationships(flow.normal->value, nest_relationships(std::move(value), path));
        }
        co_return {};
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
        flow = (co_await place(source, std::move(flow.normal->state)));
    } else {
        const auto external = [&](const auto& value) noexcept -> ContinuationTask<std::monostate> {
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
            struct NativeOperand final {
                AccessMode access;
                const SemanticExpression* expression;
            };
            auto operands = std::vector<NativeOperand>();
            visit_cpp_operands(
                value,
                [&](AccessMode access, const SemanticExpression& operand) noexcept {
                    operands.push_back({access, std::addressof(operand)});
                }
            );
            for (const auto& selected : operands) {
                const auto access = selected.access;
                const auto& operand = *selected.expression;
                const auto relationships = (co_await evaluate(operand, true));
                if (!flow.normal.has_value()) {
                    continue;
                }
                if (tracked_borrows(relationships, flow.normal->state)) {
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
                    for (const auto& target : operand_storage) {
                        if (access == AccessMode::Write) {
                            write_access(target, operand.origin);
                            writes.push_back(target);
                        }
                        accesses.push_back({target, false});
                    }
                }
            }
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
            co_return {};
        };
        (co_await source.value.visit(
            Overloaded {
                [](const SemDefault&) static noexcept -> ContinuationTask<std::monostate> {
                    co_return {};
                },
                [](const SemConstant&) static noexcept -> ContinuationTask<std::monostate> {
                    co_return {};
                },
                [&](const SemBinding&) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await place(source, std::move(flow.normal->state)));
                    co_return {};
                },
                [&](const SemCallable& value) noexcept -> ContinuationTask<std::monostate> {
                    flow.normal->value.callable_loans.push_back(
                        {{}, std::nullopt, value.callable, source.origin, false}
                    );
                    co_return {};
                },
                [](const SemEnumConstructor&) static noexcept -> ContinuationTask<std::monostate> {
                    co_return {};
                },
                [&](const SemRange& value) noexcept -> ContinuationTask<std::monostate> {
                    static_cast<void>((co_await evaluate(*value.begin)));
                    if (flow.normal) {
                        static_cast<void>((co_await evaluate(*value.end)));
                    }
                    co_return {};
                },
                [&](const SemArray& value) noexcept -> ContinuationTask<std::monostate> {
                    for (const auto [index, child] : std::views::enumerate(value.elements)) {
                        (co_await aggregate(child, OwnershipProjectionPath {index}));
                    }
                    co_return {};
                },
                [&](const SemStruct& value) noexcept -> ContinuationTask<std::monostate> {
                    for (const auto& field : value.fields) {
                        (co_await aggregate(
                            field.value,
                            OwnershipProjectionPath {field.declaration_index}
                        ));
                    }
                    co_return {};
                },
                [&](const SemEnumCase& value) noexcept -> ContinuationTask<std::monostate> {
                    for (const auto [index, child] : std::views::enumerate(value.payload)) {
                        (co_await aggregate(child, OwnershipProjectionPath {index}));
                    }
                    co_return {};
                },
                [&](const SemUnary& value) noexcept -> ContinuationTask<std::monostate> {
                    static_cast<void>((co_await evaluate(*value.operand)));
                    co_return {};
                },
                [&](const SemBinary& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous_readers = storage_readers.size();
                    protect_storage((co_await evaluate(*value.left)));
                    static_cast<void>((co_await evaluate(*value.right)));
                    restore_storage_readers(previous_readers);
                    co_return {};
                },
                [&](const SemCast& value) noexcept -> ContinuationTask<std::monostate> {
                    (co_await aggregate(*value.operand));
                    co_return {};
                },
                [&](const SemShortCircuit& value) noexcept -> ContinuationTask<std::monostate> {
                    static_cast<void>((co_await evaluate(*value.left)));
                    if (!flow.normal.has_value()) {
                        co_return {};
                    }
                    const auto known = constant_truth(*value.left);
                    const auto selected = value.operation == ShortCircuitOperator::And;
                    if (known.has_value() && *known != selected) {
                        co_return {};
                    }
                    const auto skipped = flow.normal;
                    static_cast<void>((co_await evaluate(*value.right)));
                    if (!known.has_value()) {
                        join_normal_ownership(flow.normal, skipped);
                    }
                    co_return {};
                },
                [&](const SemDereference&) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await place(source, std::move(flow.normal->state)));
                    co_return {};
                },
                [&](const SemField& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto relationships = (co_await evaluate(*value.source));
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
                    co_return {};
                },
                [&](const SemIndex& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous_readers = storage_readers.size();
                    const auto relationships = (co_await evaluate(*value.source));
                    auto storage = select_element_storage(
                        program.types(),
                        value.source->type.resolved(),
                        operand_storage,
                        relationships,
                        constant_index(*value.index)
                    );
                    protect_storage(relationships);
                    static_cast<void>((co_await evaluate(*value.index)));
                    restore_storage_readers(previous_readers);
                    if (flow.normal) {
                        if (std::holds_alternative<SliceTypeValue>(
                                program.types().type(value.source->type.resolved()).value
                            )) {
                            flow.normal->value = {};
                            for (const auto& selected : storage) {
                                merge_relationships(
                                    flow.normal->value,
                                    project_relationships(
                                        flow.normal->state.objects[selected.object].relationships,
                                        selected.path
                                    )
                                );
                            }
                        } else {
                            flow.normal->value = project_relationships(
                                relationships,
                                OwnershipProjectionPath {constant_index(*value.index)}
                            );
                        }
                        flow.normal->storage = std::move(storage);
                    }
                    co_return {};
                },
                [&](const SemReport& value) noexcept -> ContinuationTask<std::monostate> {
                    if (value.condition.has_value()) {
                        (co_await evaluate(**value.condition));
                    }
                    const auto known = value.condition.has_value()
                        ? constant_truth(**value.condition)
                        : std::optional(false);
                    const auto success = known == false ? std::nullopt : flow.normal;
                    if (known == true) {
                        co_return {};
                    }
                    if (value.message) {
                        (co_await evaluate(**value.message));
                    }
                    if (value.kind == ReportKind::Check) {
                        join_normal_ownership(flow.normal, success);
                    } else {
                        if (flow.normal && value.kind != ReportKind::Assert) {
                            flow.exits.push_back({OwnershipTestStopped {}, flow.normal->state});
                        }
                        flow.normal = success;
                    }
                    co_return {};
                },
                [&]<typename Output>(const Output& value) noexcept
                    -> ContinuationTask<std::monostate> {
                    const auto previous_accesses = accesses.size();
                    const auto previous_readers = storage_readers.size();
                    // SemFormat instantiations assign the receiver places below.
                    // NOLINTNEXTLINE(misc-const-correctness)
                    auto destinations = std::vector<OwnershipPlace>();
                    auto formatting_reads = OwnershipRelationships {};
                    if constexpr (std::same_as<Output, SemFormat>) {
                        if (value.receiver) {
                            protect_storage((co_await evaluate(**value.receiver)));
                            destinations = operand_storage;
                            for (const auto& target : destinations) {
                                accesses.push_back({target, false});
                            }
                        }
                    }
                    for (const auto& operand : value.operands) {
                        const auto relationships = (co_await evaluate(operand.expression, true));
                        if (!flow.normal) {
                            break;
                        }
                        if (tracked_borrows(relationships, flow.normal->state)) {
                            diagnose(
                                DiagnosticCode::TypeCallableViewEscape,
                                "tracked callable borrows cannot cross a C++ formatter contract",
                                source.origin
                            );
                        }
                        protect_storage(relationships);
                        if (!destinations.empty()
                            && program.types().type(operand.expression.type.resolved()).value
                                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}) {
                            for (const auto& backing : operand_storage) {
                                formatting_reads.storage_loans.push_back(
                                    {{}, backing, operand.expression.origin}
                                );
                            }
                        }
                        if (!std::holds_alternative<PointerTypeValue>(
                                program.types().type(operand.expression.type.resolved()).value
                            )) {
                            for (const auto& target : operand_storage) {
                                accesses.push_back({target, false});
                            }
                        }
                    }
                    if (flow.normal) {
                        for (const auto& destination : destinations) {
                            // String Read aliases are observed after all holes complete.
                            // They must remain separate from the destination during append.
                            protect_storage(formatting_reads);
                            write_access(destination, source.origin);
                            check_storage_write(flow.normal->state, destination, source.origin);
                        }
                        flow.normal->value = {};
                    }
                    restore_storage_readers(previous_readers);
                    accesses.resize(previous_accesses);
                    co_return {};
                },
                [&](const SemSliceIntrinsic& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous_readers = storage_readers.size();
                    auto relationships = (co_await evaluate(value.operands.front().expression));
                    if (value.intrinsic == SliceIntrinsic::FromArray) {
                        // A slice refers to backing storage; nested relationships remain
                        // on that storage and are selected only when an element is read.
                        relationships = {};
                        for (const auto& backing : operand_storage) {
                            relationships.storage_loans.push_back({{}, backing, source.origin});
                        }
                    }
                    protect_storage(relationships);
                    for (auto i = 1uz; i < value.operands.size(); ++i) {
                        static_cast<void>((co_await evaluate(value.operands[i].expression)));
                    }
                    if (flow.normal) {
                        flow.normal->value = value.intrinsic == SliceIntrinsic::FromArray
                                || value.intrinsic == SliceIntrinsic::Slice
                            ? std::move(relationships)
                            : OwnershipRelationships {};
                    }
                    restore_storage_readers(previous_readers);
                    co_return {};
                },
                [&](const SemTextIntrinsic& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous_accesses = accesses.size();
                    const auto previous_readers = storage_readers.size();
                    auto borrowed = OwnershipRelationships {};
                    auto receiver_storage = std::vector<OwnershipPlace>();
                    for (const auto& [index, operand] : std::views::enumerate(value.operands)) {
                        auto relationships = (co_await evaluate(operand.expression));
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
                            receiver_storage = operand_storage;
                            for (const auto& selected : receiver_storage) {
                                accesses.push_back({selected, false});
                            }
                        }
                    }
                    if (flow.normal) {
                        if (text_intrinsic_writes(value.intrinsic)) {
                            for (const auto& target : receiver_storage) {
                                write_access(target, source.origin);
                                check_storage_write(flow.normal->state, target, source.origin);
                            }
                        }
                        flow.normal->value = std::move(borrowed);
                    }
                    restore_storage_readers(previous_readers);
                    accesses.resize(previous_accesses);
                    co_return {};
                },
                [&](const SemArrayAdopt& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto original = (co_await evaluate(*value.source));
                    if (!flow.normal.has_value()) {
                        co_return {};
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
                    co_return {};
                },
                [&](const SemBorrowCallable& value) noexcept -> ContinuationTask<std::monostate> {
                    if (std::holds_alternative<SemTake>(value.source->value)
                        && analysis.contents(value.source->type.resolved()).callable_view
                        && value.source->type.resolved() != source.type.resolved()) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "taken callable storage cannot back a widened view",
                            source.origin
                        );
                    }
                    const auto relationships = (co_await evaluate(*value.source));
                    const auto& backing = operand_storage;
                    if (!flow.normal.has_value()) {
                        co_return {};
                    }
                    flow.normal->value = relationships;
                    // Equal view types copy the target description. They do not
                    // borrow the intermediate view's storage.
                    if (value.source->type.resolved() == source.type.resolved()) {
                        co_return {};
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
                    co_return {};
                },
                [&](const SemTake& value) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await place(*value.place, std::move(flow.normal->state)));
                    if (!flow.normal.has_value()) {
                        co_return {};
                    }
                    const auto target = flow.normal->storage.front();
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
                    owner.modified = true;
                    if (!owner.taken.has_value()) {
                        owner.taken = source.origin;
                    }
                    owner.relationships = {};
                    co_return {};
                },
                [&](const SemClosure& value) noexcept -> ContinuationTask<std::monostate> {
                    for (const auto [index, capture] : std::views::enumerate(value.captures)) {
                        if (!flow.normal.has_value()) {
                            break;
                        }
                        if (capture.mode == CaptureMode::Write) {
                            auto accumulated = std::move(flow.normal->value);
                            auto selected =
                                (co_await place(capture.expression, std::move(flow.normal->state)));
                            flow.normal = std::move(selected.normal);
                            append_ownership_exits(flow, selected);
                            if (!flow.normal.has_value()) {
                                break;
                            }
                            flow.normal->value = std::move(accumulated);
                            for (const auto& target : flow.normal->storage) {
                                write_access(target, capture.expression.origin);
                                flow.normal->value.captures.push_back(
                                    {OwnershipProjectionPath {index}, target, source.origin}
                                );
                            }
                        } else {
                            (co_await aggregate(
                                capture.expression,
                                OwnershipProjectionPath {index}
                            ));
                        }
                    }
                    co_return {};
                },
                [&](const SemCpp& value) noexcept -> ContinuationTask<std::monostate> {
                    (co_await external(value));
                    co_return {};
                },
                [&](const SemCppCall& value) noexcept -> ContinuationTask<std::monostate> {
                    (co_await external(value));
                    co_return {};
                },
                [&](const SemCall& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous = accesses.size();
                    const auto previous_readers = storage_readers.size();
                    const auto concrete = callable_for(value.callee->type.resolved());
                    auto callee = OwnershipRelationships {};
                    auto callee_storage = std::vector<OwnershipPlace>();
                    if (concrete.has_value()
                        && value.callee->category == SemanticValueCategory::Place) {
                        auto located =
                            (co_await place(*value.callee, std::move(flow.normal->state)));
                        flow.normal = std::move(located.normal);
                        append_ownership_exits(flow, located);
                        callee =
                            flow.normal ? std::move(flow.normal->value) : OwnershipRelationships {};
                        if (flow.normal) {
                            callee_storage = flow.normal->storage;
                        }
                    } else {
                        callee = (co_await evaluate(*value.callee));
                        callee_storage = operand_storage;
                    }
                    if (concrete.has_value()) {
                        for (const auto& selected : callee_storage) {
                            accesses.push_back({selected, false});
                        }
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
                        auto relationships = (co_await evaluate(argument.expression, true));
                        if (!flow.normal.has_value()) {
                            break;
                        }
                        const auto snapshot =
                            argument.access == AccessMode::Read
                            && std::holds_alternative<PointerTypeValue>(
                                program.types().type(argument.expression.type.resolved()).value
                            );
                        auto alias = std::optional<OwnershipPlace>();
                        auto storage = std::vector<OwnershipPlace>();
                        if (argument.access != AccessMode::Take
                            && !snapshot
                            && (argument.expression.selects_storage()
                                || argument.expression.category == SemanticValueCategory::Place)) {
                            if (operand_storage.size() == 1uz) {
                                alias = operand_storage.front();
                            } else {
                                storage = operand_storage;
                            }
                        }
                        if (argument.access == AccessMode::Read
                            && analysis.contents(argument.expression.type.resolved())
                                   .read_borrows_storage()) {
                            storage = operand_storage;
                            alias.reset();
                            for (const auto& selected : storage) {
                                accesses.push_back({selected, false});
                            }
                        }
                        if (argument.access == AccessMode::Write) {
                            for (const auto& target : storage) {
                                write_access(target, argument.expression.origin);
                                accesses.push_back({target, false});
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
                            {.alias = std::move(alias),
                             .value = std::move(relationships),
                             .storage = std::move(storage),
                             .capture_holder = std::nullopt}
                        );
                    }
                    if (flow.normal.has_value()) {
                        auto invoked = OwnershipFlow {};
                        const auto invoke = [&](
                                                this const auto& self,
                                                const OwnershipRelationships& target,
                                                std::optional<CallableID> function,
                                                std::optional<OwnershipPlace> capture_owner
                                            ) noexcept -> ContinuationTask<std::monostate> {
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
                                co_return {};
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
                                    (co_await self(backing, loan.callable, loan.backing));
                                } else if (loan.callable.has_value()) {
                                    (co_await self({}, loan.callable, std::nullopt));
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
                            co_return {};
                        };
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
                                (co_await invoke(
                                    project_relationships(
                                        flow.normal->state.objects[backing.object].relationships,
                                        backing.path
                                    ),
                                    concrete,
                                    backing
                                ));
                            }
                        } else {
                            (co_await invoke(callee, concrete, std::nullopt));
                        }
                        flow.normal = std::move(invoked.normal);

                        append_ownership_exits(flow, invoked);
                    }
                    accesses.resize(previous);
                    restore_storage_readers(previous_readers);
                    co_return {};
                },
                [&](const SemPropagate& value) noexcept -> ContinuationTask<std::monostate> {
                    auto propagated = (co_await evaluate(*value.operand, direct));
                    if (flow.normal) {
                        flow.normal->value = std::move(propagated);
                    }
                    co_return {};
                },
                [&](const SemIf& value) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await conditional(value, std::move(flow.normal->state)));
                    co_return {};
                },
                [&](const SemMatch& value) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await match(value, std::move(flow.normal->state)));
                    co_return {};
                },
                [&](const SemTry& value) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await attempt(value, std::move(flow.normal->state)));
                    co_return {};
                },
            }
        ));
    }
    if (flow.normal.has_value()) {
        if (!source.selects_storage() && source.category != SemanticValueCategory::Place) {
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
    co_return flow;
}
