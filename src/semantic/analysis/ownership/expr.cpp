module carven:semantic.analysis.ownership.expr.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.callable;
import :semantic.semir.evaluation;
import :semantic.semir.vector;
import :support.invariant;
import std;

auto OwnershipBodyAnalyzer::opaque_callable_targets(
    ConstructionTypeRef type,
    ProgramOriginID origin
) const noexcept -> OwnershipRelationships {
    auto result = OwnershipRelationships {};
    auto path = OwnershipProjectionPath {};
    const auto collect = [&](this auto&& self, ConstructionTypeRef reference) noexcept -> void {
        const auto& shape = analysis.model.shape(reference);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&shape)) {
            if (array->extent == 0uz) {
                return;
            }
            path.push_back(std::nullopt);
            self(array->element);
            path.pop_back();
        } else if (const auto* tuple = std::get_if<ConstructionTupleTypeValue>(&shape)) {
            for (const auto [index, element] : std::views::enumerate(tuple->elements)) {
                path.push_back(static_cast<std::size_t>(index));
                self(element);
                path.pop_back();
            }
        } else if (std::holds_alternative<CallableViewTypeValue>(shape)
                   || std::holds_alternative<ClosureTypeValue>(shape)) {
            result.edit().callable_loans.push_back(
                OwnershipCallableLoan {
                    .holder = path,
                    .backing = std::nullopt,
                    .callable = std::nullopt,
                    .origin = origin,
                    .direct_only = false
                }
            );
        }
    };
    collect(type);
    return result;
}

auto OwnershipBodyAnalyzer::selected_access_kind(const SemanticExpression& source) const noexcept
    -> OwnershipAccessKind {
    if (const auto* index = std::get_if<SemIndex>(&source.value)) {
        if (std::holds_alternative<GenericVectorType>(
                analysis.model.shape(index->source->type.construction())
            )) {
            return OwnershipAccessKind::Structural;
        }
        return selected_access_kind(*index->source);
    }
    if (const auto* tuple = std::get_if<SemTupleElement>(&source.value)) {
        return selected_access_kind(*tuple->source);
    }
    if (const auto* field = std::get_if<SemField>(&source.value)) {
        return selected_access_kind(*field->source);
    }
    if (const auto* foreign = std::get_if<SemCpp>(&source.value)) {
        return selected_access_kind(foreign->operands.front().expression);
    }
    return OwnershipAccessKind::Active;
}

auto OwnershipBodyAnalyzer::complete_place(
    const SemanticExpression& source,
    OwnershipNormal& normal,
    bool read
) noexcept -> void {
    if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
        normal.storage = binding_places(binding->binding, normal.state);
    }
    if (normal.storage.empty()) {
        normal.value = opaque_callable_targets(source.type.construction(), source.origin);
        return;
    }
    normal.value = {};
    for (const auto& target : normal.storage) {
        if (read) {
            require_available(normal.state, target, source.origin);
        }
        merge_relationships(
            normal.value,
            project_relationships(normal.state.objects[target.object].relationships, target.path)
        );
    }
    if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
        const auto* parameter =
            std::get_if<ParameterBindingStorage>(&body.binding(binding->binding).storage);
        if (parameter != nullptr
            && parameter->access == AccessMode::Read
            && aliases.contains(binding->binding)) {
            merge_relationships(
                normal.value,
                normal.state.objects[input.objects.size() + binding->binding.index()].relationships
            );
        }
    }
}

auto OwnershipBodyAnalyzer::place(
    const SemanticExpression& source,
    OwnershipState state,
    bool read
) noexcept -> ContinuationTask<OwnershipFlow> {
    synchronize_storage(state);
    auto result = OwnershipFlow {
        .normal = OwnershipNormal {.state = std::move(state), .value = {}, .storage = {}},
        .exits = {}
    };
    if (const auto* dereference = std::get_if<SemDereference>(&source.value)) {
        auto next =
            (co_await expression(*dereference->source, std::move(result.normal->state), false));
        result.normal = std::move(next.normal);
        append_ownership_exits(result, next);
        if (result.normal) {
            result.normal->storage.clear();
        }
    } else if (const auto* foreign = std::get_if<SemCpp>(&source.value)) {
        result = (co_await place(
            foreign->operands.front().expression,
            std::move(result.normal->state),
            true
        ));
        for (const auto& operand : std::span(foreign->operands).subspan(1)) {
            if (!result.normal.has_value()) {
                break;
            }
            const auto previous = accesses.size();
            auto storage = result.normal->storage;
            for (const auto& target : storage) {
                accesses.push_back(
                    OwnershipAccess {
                        .place = target,
                        .kind = selected_access_kind(source),
                        .descendants = false
                    }
                );
            }
            auto next =
                (co_await expression(operand.expression, std::move(result.normal->state), false));
            accesses.resize(previous);
            result.normal = std::move(next.normal);
            if (result.normal) {
                result.normal->storage = std::move(storage);
            }
            append_ownership_exits(result, next);
        }
    } else if (const auto* tuple = std::get_if<SemTupleElement>(&source.value)) {
        result = (co_await place(*tuple->source, std::move(result.normal->state), true));
        if (result.normal) {
            for (auto& selected : result.normal->storage) {
                selected.path.push_back(tuple->index);
            }
        }
    } else if (const auto* field = std::get_if<SemField>(&source.value)) {
        result = (co_await place(*field->source, std::move(result.normal->state), true));
        if (result.normal) {
            for (auto& selected : result.normal->storage) {
                selected.path.push_back(field->field.field_index);
            }
        }
    } else if (const auto* index = std::get_if<SemIndex>(&source.value)) {
        const auto slice = std::holds_alternative<ConstructionSliceTypeValue>(
            analysis.model.shape(index->source->type.construction())
        );
        result = slice
            ? (co_await expression(*index->source, std::move(result.normal->state), false))
            : (co_await place(*index->source, std::move(result.normal->state), true));
        if (result.normal.has_value()) {
            const auto relationships = std::move(result.normal->value);
            auto storage = select_element_storage(
                index->source->type.construction(),
                result.normal->storage,
                relationships,
                constant_index(*index->index),
                source.origin,
                result.normal->state
            );
            const auto previous = accesses.size();
            const auto previous_readers = storage_readers.size();
            protect_storage(relationships);
            for (const auto& target : storage) {
                accesses.push_back(
                    OwnershipAccess {
                        .place = target,
                        .kind = selected_access_kind(source),
                        .descendants = false
                    }
                );
            }
            auto indexed =
                (co_await expression(*index->index, std::move(result.normal->state), false));
            accesses.resize(previous);
            restore_storage_readers(previous_readers);
            result.normal = std::move(indexed.normal);
            if (result.normal) {
                result.normal->storage = std::move(storage);
            }
            append_ownership_exits(result, indexed);
        }
    }
    if (result.normal.has_value()) {
        complete_place(source, *result.normal, read);
    }
    co_return result;
}

auto OwnershipBodyAnalyzer::is_leaf_expression(const SemanticExpression& source) noexcept -> bool {
    return std::holds_alternative<SemBinding>(source.value)
        || (std::holds_alternative<SemConstant>(source.value)
            && source.category != SemanticValueCategory::Place);
}

auto OwnershipBodyAnalyzer::finish_expression(
    const SemanticExpression& source,
    OwnershipNormal& normal,
    bool direct
) noexcept -> void {
    if (!source.selects_storage() && source.category != SemanticValueCategory::Place) {
        normal.storage.clear();
        if (const auto found = facts.temporaries.find(std::addressof(source));
            found != facts.temporaries.end()) {
            normal.storage.push_back(
                OwnershipPlace {.object = input.objects.size() + found->second, .path = {}}
            );
        }
    }
    use(normal.value, normal.state, source.origin, direct);
    if (source.category == SemanticValueCategory::Value) {
        retain(normal.state, normal.value, source);
    }
}

auto OwnershipBodyAnalyzer::complete_leaf_expression(
    const SemanticExpression& source,
    OwnershipNormal& normal,
    bool direct
) noexcept -> void {
    const auto binding = std::holds_alternative<SemBinding>(source.value);
    if (!binding
        && (!std::holds_alternative<SemConstant>(source.value)
            || source.category == SemanticValueCategory::Place)) {
        invariant_violation("non-leaf expression reached synchronous ownership completion");
    }
    // A child starts with empty result fields while sharing the current state.
    normal.value = {};
    normal.storage.clear();
    if (binding) {
        complete_place(source, normal, true);
    }
    finish_expression(source, normal, direct);
}

auto OwnershipBodyAnalyzer::expression(
    const SemanticExpression& source,
    OwnershipState state,
    bool direct
) noexcept -> ContinuationTask<OwnershipFlow> {
    synchronize_storage(state);
    auto flow = OwnershipFlow {
        .normal = OwnershipNormal {.state = std::move(state), .value = {}, .storage = {}},
        .exits = {}
    };
    auto operand_storage = std::vector<OwnershipPlace>();
    const auto evaluate = [&](const SemanticExpression& child,
                              bool argument) noexcept -> ContinuationTask<OwnershipRelationships> {
        if (!flow.normal.has_value()) {
            co_return {};
        }
        auto accumulated = std::move(flow.normal->value);
        auto storage = std::move(flow.normal->storage);
        const auto previous_readers = storage_readers.size();
        protect_storage(accumulated);
        if (is_leaf_expression(child)) {
            complete_leaf_expression(child, *flow.normal, argument);
            restore_storage_readers(previous_readers);
            auto value = std::move(flow.normal->value);
            operand_storage = std::move(flow.normal->storage);
            flow.normal->value = std::move(accumulated);
            flow.normal->storage = std::move(storage);
            co_return value;
        }
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
        auto value = (co_await evaluate(child, false));
        if (flow.normal) {
            merge_relationships(flow.normal->value, nest_relationships(std::move(value), path));
        }
        co_return {};
    };
    if (is_leaf_expression(source)) {
        complete_leaf_expression(source, *flow.normal, direct);
        co_return flow;
    } else if (source.category == SemanticValueCategory::Place) {
        flow = (co_await place(source, std::move(flow.normal->state), true));
    } else {
        const auto external = [&](const auto& value) noexcept -> ContinuationTask<std::monostate> {
            if (analysis.contents(source.type.construction()).contains_callable_view) {
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
                    operands.push_back(
                        NativeOperand {.access = access, .expression = std::addressof(operand)}
                    );
                }
            );
            for (const auto& selected : operands) {
                const auto access = selected.access;
                const auto& operand = *selected.expression;
                const auto relationships = (co_await evaluate(operand, true));
                if (!flow.normal.has_value()) {
                    continue;
                }
                if (diagnosing && tracked_borrows(relationships, flow.normal->state)) {
                    diagnose(
                        DiagnosticCode::TypeCallableViewEscape,
                        "tracked borrows cannot cross an undeclared C++ contract",
                        source.origin
                    );
                }
                protect_storage(relationships);
                const auto snapshot = access == AccessMode::Read
                    && std::holds_alternative<AnalysisPointerType>(
                                          analysis.model.shape(operand.type.construction())
                    );
                if (access != AccessMode::Take && !snapshot) {
                    for (const auto& target : operand_storage) {
                        if (access == AccessMode::Write) {
                            write_access(target, operand.origin, true);
                            writes.push_back(target);
                        }
                        accesses.push_back(
                            OwnershipAccess {
                                .place = target,
                                .kind = selected_access_kind(operand),
                                .descendants = false
                            }
                        );
                    }
                }
            }
            if (flow.normal) {
                // Native Write may leave the old view in place; keep its known loans.
                for (const auto& target : writes) {
                    check_storage_write(flow.normal->state, target, source.origin, false);
                }
                // Native results obey the provider/caller storage contract.
                flow.normal->value = {};
            }
            restore_storage_readers(previous_readers);
            accesses.resize(previous);
            co_return {};
        };
        const auto protect = [&](const OwnershipRelationships& relationships,
                                 bool storage) noexcept {
            if (storage) {
                protect_storage(relationships);
            }
            for (const auto& loan : relationships.view().callable_loans) {
                if (loan.backing.has_value()) {
                    accesses.push_back(
                        OwnershipAccess {
                            .place = *loan.backing,
                            .kind = OwnershipAccessKind::Active,
                            .descendants = false
                        }
                    );
                }
            }
        };
        const auto prepare_call_argument = [&](const SemCallArgument& argument,
                                               bool body_known) noexcept
            -> ContinuationTask<std::optional<OwnershipCallArgument>> {
            auto relationships = (co_await evaluate(argument.expression, true));
            if (!flow.normal.has_value()) {
                co_return std::nullopt;
            }
            const auto snapshot = argument.access == AccessMode::Read
                && std::holds_alternative<AnalysisPointerType>(
                                      analysis.model.shape(argument.expression.type.construction())
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
                && analysis.contents(argument.expression.type.construction())
                       .read_borrows_storage()) {
                storage = operand_storage;
                alias.reset();
                for (const auto& selected : storage) {
                    accesses.push_back(
                        OwnershipAccess {
                            .place = selected,
                            .kind = selected_access_kind(argument.expression),
                            .descendants = false
                        }
                    );
                }
            }
            if (argument.access == AccessMode::Write) {
                for (const auto& target : storage) {
                    write_access(target, argument.expression.origin, !body_known);
                    accesses.push_back(
                        OwnershipAccess {
                            .place = target,
                            .kind = selected_access_kind(argument.expression),
                            .descendants = false
                        }
                    );
                }
            }
            if (alias.has_value()) {
                if (argument.access == AccessMode::Write) {
                    write_access(*alias, argument.expression.origin, !body_known);
                }
                if (argument.access != AccessMode::Read
                    || !analysis.contents(argument.expression.type.construction())
                            .read_is_value_snapshot()) {
                    accesses.push_back(
                        OwnershipAccess {
                            .place = *alias,
                            .kind = selected_access_kind(argument.expression),
                            .descendants = false
                        }
                    );
                }
            }
            protect(relationships, argument.access != AccessMode::Write);
            co_return OwnershipCallArgument {
                .alias = std::move(alias),
                .value = std::move(relationships),
                .storage = std::move(storage),
                .capture_holder = std::nullopt
            };
        };
        const auto retain_cold_arguments =
            [&](std::span<const SemCallArgument> arguments,
                const auto& parameters) noexcept -> ContinuationTask<std::monostate> {
            for (const auto& [argument, parameter] : std::views::zip(arguments, parameters)) {
                auto relationships = co_await evaluate(argument.expression, true);
                if (!flow.normal) {
                    break;
                }
                if (parameter.stage == ParameterStage::Static) {
                    continue;
                }
                const auto borrowed = argument.access == AccessMode::Write
                    || (argument.access == AccessMode::Read
                        && !analysis.contents(argument.expression.type.construction())
                                .read_is_value_snapshot());
                if (borrowed) {
                    for (const auto& place : operand_storage) {
                        relationships.edit().storage_loans.push_back(
                            OwnershipStorageLoan {
                                .holder = {},
                                .backing = place,
                                .origin = argument.expression.origin,
                                .protection = OwnershipLoanProtection::Lifetime
                            }
                        );
                    }
                    if (operand_storage.empty()) {
                        diagnose(
                            DiagnosticCode::AsyncOwnership,
                            "cold borrow requires known source backing",
                            argument.expression.origin
                        );
                    }
                }
                merge_relationships(flow.normal->value, relationships);
            }
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
                [&](const SemUnreachable&) noexcept -> ContinuationTask<std::monostate> {
                    flow.normal.reset();
                    co_return {};
                },
                [&](const SemBinding&) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await place(source, std::move(flow.normal->state), true));
                    co_return {};
                },
                [&](const SemCallable& value) noexcept -> ContinuationTask<std::monostate> {
                    flow.normal->value.edit().callable_loans.push_back(
                        OwnershipCallableLoan {
                            .holder = {},
                            .backing = std::nullopt,
                            .callable = value.callable,
                            .origin = source.origin,
                            .direct_only = false
                        }
                    );
                    co_return {};
                },
                [](const SemEnumConstructor&) static noexcept -> ContinuationTask<std::monostate> {
                    co_return {};
                },
                [&](const SemRange& value) noexcept -> ContinuationTask<std::monostate> {
                    static_cast<void>((co_await evaluate(*value.begin, false)));
                    if (flow.normal) {
                        static_cast<void>((co_await evaluate(*value.end, false)));
                    }
                    co_return {};
                },
                [&]<typename Product>(const Product& value) noexcept
                    -> ContinuationTask<std::monostate>
                    requires std::same_as<Product, SemArray> || std::same_as<Product, SemTuple>
                {
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
                    static_cast<void>((co_await evaluate(*value.operand, false)));
                    co_return {};
                },
                [&](const SemBinary& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous_readers = storage_readers.size();
                    protect_storage((co_await evaluate(*value.left, false)));
                    static_cast<void>((co_await evaluate(*value.right, false)));
                    restore_storage_readers(previous_readers);
                    co_return {};
                },
                [&](const SemCast& value) noexcept -> ContinuationTask<std::monostate> {
                    (co_await aggregate(*value.operand));
                    co_return {};
                },
                [&](const SemShortCircuit& value) noexcept -> ContinuationTask<std::monostate> {
                    static_cast<void>((co_await evaluate(*value.left, false)));
                    if (!flow.normal.has_value()) {
                        co_return {};
                    }
                    const auto truth = known_boolean(program, *value.left);
                    const auto runs_right = truth
                        ? std::optional(*truth == (value.operation == ShortCircuitOperator::And))
                        : std::nullopt;
                    if (runs_right == false) {
                        co_return {};
                    }
                    const auto skipped = runs_right ? std::nullopt : flow.normal;
                    static_cast<void>((co_await evaluate(*value.right, false)));
                    join_normal_ownership(flow.normal, skipped);
                    co_return {};
                },
                [&](const SemDereference&) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await place(source, std::move(flow.normal->state), true));
                    co_return {};
                },
                [&](const SemAddressOf& value) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await place(*value.source, std::move(flow.normal->state), true));
                    if (flow.normal) {
                        flow.normal->value = {};
                        flow.normal->storage.clear();
                    }
                    co_return {};
                },
                [&](const SemTupleElement& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto relationships = (co_await evaluate(*value.source, false));
                    if (flow.normal) {
                        flow.normal->value = project_relationships(
                            relationships,
                            OwnershipProjectionPath {value.index}
                        );
                        flow.normal->storage = std::move(operand_storage);
                        for (auto& selected : flow.normal->storage) {
                            selected.path.push_back(value.index);
                        }
                    }
                    co_return {};
                },
                [&](const SemField& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto relationships = (co_await evaluate(*value.source, false));
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
                    const auto relationships = (co_await evaluate(*value.source, false));
                    if (!flow.normal) {
                        co_return {};
                    }
                    auto storage = select_element_storage(
                        value.source->type.construction(),
                        operand_storage,
                        relationships,
                        constant_index(*value.index),
                        source.origin,
                        flow.normal->state
                    );
                    protect_storage(relationships);
                    const auto previous_accesses = accesses.size();
                    for (const auto& selected : storage) {
                        accesses.push_back(
                            OwnershipAccess {
                                .place = selected,
                                .kind = selected_access_kind(source),
                                .descendants = false
                            }
                        );
                    }
                    static_cast<void>((co_await evaluate(*value.index, false)));
                    accesses.resize(previous_accesses);
                    restore_storage_readers(previous_readers);
                    if (flow.normal) {
                        if (std::holds_alternative<ConstructionSliceTypeValue>(
                                analysis.model.shape(value.source->type.construction())
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
                        (co_await evaluate(**value.condition, false));
                    }
                    const auto success = value.condition.has_value() ? flow.normal : std::nullopt;
                    if (value.message) {
                        (co_await evaluate(**value.message, false));
                    }
                    if (value.kind == ReportKind::Check) {
                        join_normal_ownership(flow.normal, success);
                    } else {
                        if (flow.normal && value.kind != ReportKind::Assert) {
                            flow.exits.push_back(
                                OwnershipExit {
                                    .payload = OwnershipTestStopped {},
                                    .state = flow.normal->state
                                }
                            );
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
                            protect_storage((co_await evaluate(**value.receiver, false)));
                            destinations = operand_storage;
                            for (const auto& target : destinations) {
                                accesses.push_back(
                                    OwnershipAccess {
                                        .place = target,
                                        .kind = selected_access_kind(**value.receiver),
                                        .descendants = false
                                    }
                                );
                            }
                        }
                    }
                    for (const auto& operand : value.operands) {
                        const auto relationships = (co_await evaluate(operand.expression, true));
                        if (!flow.normal) {
                            break;
                        }
                        if (diagnosing && tracked_borrows(relationships, flow.normal->state)) {
                            diagnose(
                                DiagnosticCode::TypeCallableViewEscape,
                                "tracked callable borrows cannot cross a C++ formatter contract",
                                source.origin
                            );
                        }
                        protect_storage(relationships);
                        if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                                &analysis.model.shape(operand.expression.type.construction())
                            );
                            !destinations.empty()
                            && builtin
                            && builtin->kind == BuiltinType::String) {
                            for (const auto& backing : operand_storage) {
                                formatting_reads.edit().storage_loans.push_back(
                                    OwnershipStorageLoan {
                                        .holder = {},
                                        .backing = backing,
                                        .origin = operand.expression.origin,
                                        .protection = OwnershipLoanProtection::Contents
                                    }
                                );
                            }
                        }
                        if (!std::holds_alternative<AnalysisPointerType>(
                                analysis.model.shape(operand.expression.type.construction())
                            )) {
                            for (const auto& target : operand_storage) {
                                accesses.push_back(
                                    OwnershipAccess {
                                        .place = target,
                                        .kind = selected_access_kind(operand.expression),
                                        .descendants = false
                                    }
                                );
                            }
                        }
                    }
                    if (flow.normal) {
                        for (const auto& destination : destinations) {
                            // String Read aliases are observed after all holes complete.
                            // They must remain separate from the destination during append.
                            protect_storage(formatting_reads);
                            write_access(destination, source.origin, true);
                            check_storage_write(
                                flow.normal->state,
                                destination,
                                source.origin,
                                false
                            );
                        }
                        flow.normal->value = {};
                    }
                    restore_storage_readers(previous_readers);
                    accesses.resize(previous_accesses);
                    co_return {};
                },
                [&](const SemIntrinsic& value) noexcept -> ContinuationTask<std::monostate> {
                    co_return co_await value.operation.visit(
                        Overloaded {
                            [&](const SliceIntrinsicOperation& family) noexcept
                                -> ContinuationTask<std::monostate> {
                                const auto previous_readers = storage_readers.size();
                                auto relationships =
                                    (co_await evaluate(value.operands.front().expression, false));
                                if (family.intrinsic == SliceIntrinsic::View
                                    && !std::holds_alternative<ConstructionSliceTypeValue>(
                                        analysis.model.shape(
                                            value.operands.front().expression.type.construction()
                                        )
                                    )) {
                                    // A slice refers to backing storage; nested relationships remain
                                    // on that storage and are selected only when an element is read.
                                    relationships = {};
                                    if (family.result_extent != 0) {
                                        for (const auto& backing : operand_storage) {
                                            relationships.edit().storage_loans.push_back(
                                                OwnershipStorageLoan {
                                                    .holder = {},
                                                    .backing = backing,
                                                    .origin = source.origin,
                                                    .protection = OwnershipLoanProtection::Contents
                                                }
                                            );
                                        }
                                    }
                                }
                                protect_storage(relationships);
                                for (auto i = 1uz; i < value.operands.size(); ++i) {
                                    static_cast<void>(
                                        (co_await evaluate(value.operands[i].expression, false))
                                    );
                                }
                                if (flow.normal) {
                                    flow.normal->value = family.intrinsic == SliceIntrinsic::View
                                            || family.intrinsic == SliceIntrinsic::Slice
                                        ? std::move(relationships)
                                        : OwnershipRelationships {};
                                }
                                restore_storage_readers(previous_readers);
                                co_return {};
                            },
                            [&](const FloatIntrinsic&) noexcept
                                -> ContinuationTask<std::monostate> {
                                for (const auto& operand : value.operands) {
                                    static_cast<void>(co_await evaluate(operand.expression, false));
                                }
                                if (flow.normal) {
                                    flow.normal->value = OwnershipRelationships {};
                                }
                                co_return {};
                            },
                            [&](const SIMDIntrinsic&) noexcept -> ContinuationTask<std::monostate> {
                                const auto previous_readers = storage_readers.size();
                                for (const auto& operand : value.operands) {
                                    const auto relationships =
                                        co_await evaluate(operand.expression, false);
                                    protect_storage(relationships);
                                }
                                if (flow.normal) {
                                    flow.normal->value = OwnershipRelationships {};
                                }
                                restore_storage_readers(previous_readers);
                                co_return {};
                            },
                            [&](const VectorIntrinsicOperation& family) noexcept
                                -> ContinuationTask<std::monostate> {
                                const auto contract = vector_intrinsic_contract(family.intrinsic);
                                const auto previous_accesses = accesses.size();
                                const auto previous_readers = storage_readers.size();
                                auto receiver_storage = std::vector<OwnershipPlace>();
                                for (const auto& [index, operand] :
                                     std::views::enumerate(value.operands)) {
                                    const auto relationships =
                                        co_await evaluate(operand.expression, false);
                                    if (!flow.normal) {
                                        break;
                                    }
                                    protect_storage(relationships);
                                    if (index == 0) {
                                        receiver_storage = operand_storage;
                                        for (const auto& target : receiver_storage) {
                                            accesses.push_back(
                                                OwnershipAccess {
                                                    .place = target,
                                                    .kind =
                                                        selected_access_kind(operand.expression),
                                                    .descendants = false
                                                }
                                            );
                                        }
                                    }
                                }
                                if (flow.normal) {
                                    if (contract.receiver_access == AccessMode::Write) {
                                        for (const auto& target : receiver_storage) {
                                            write_access(target, source.origin, true);
                                            check_storage_write(
                                                flow.normal->state,
                                                target,
                                                source.origin,
                                                false
                                            );
                                        }
                                    }
                                    // The element admission contract excludes contained loans.
                                    flow.normal->value = {};
                                }
                                restore_storage_readers(previous_readers);
                                accesses.resize(previous_accesses);
                                co_return {};
                            },
                            [&](const TextIntrinsic& family) noexcept
                                -> ContinuationTask<std::monostate> {
                                const auto previous_accesses = accesses.size();
                                const auto previous_readers = storage_readers.size();
                                auto borrowed = OwnershipRelationships {};
                                auto receiver_storage = std::vector<OwnershipPlace>();
                                for (const auto& [index, operand] :
                                     std::views::enumerate(value.operands)) {
                                    auto relationships =
                                        (co_await evaluate(operand.expression, false));
                                    if (!flow.normal) {
                                        break;
                                    }
                                    if (index == 0
                                        && (family == TextIntrinsic::FromUTF8Unchecked
                                            || family == TextIntrinsic::AsStr
                                            || family == TextIntrinsic::Bytes
                                            || family == TextIntrinsic::Chars)) {
                                        if (const auto* builtin =
                                                std::get_if<BuiltinTypeValue>(&analysis.model.shape(
                                                    operand.expression.type.construction()
                                                ));
                                            builtin && builtin->kind == BuiltinType::String) {
                                            for (const auto& backing : operand_storage) {
                                                relationships.edit().storage_loans.push_back(
                                                    OwnershipStorageLoan {
                                                        .holder = {},
                                                        .backing = backing,
                                                        .origin = source.origin,
                                                        .protection =
                                                            OwnershipLoanProtection::Contents
                                                    }
                                                );
                                            }
                                        }
                                        borrowed = relationships;
                                    }
                                    protect_storage(relationships);
                                    if (index == 0) {
                                        receiver_storage = operand_storage;
                                        for (const auto& selected : receiver_storage) {
                                            accesses.push_back(
                                                OwnershipAccess {
                                                    .place = selected,
                                                    .kind =
                                                        selected_access_kind(operand.expression),
                                                    .descendants = false
                                                }
                                            );
                                        }
                                    }
                                }
                                if (flow.normal) {
                                    if (text_intrinsic_writes(family)) {
                                        for (const auto& target : receiver_storage) {
                                            write_access(target, source.origin, true);
                                            check_storage_write(
                                                flow.normal->state,
                                                target,
                                                source.origin,
                                                false
                                            );
                                        }
                                    }
                                    flow.normal->value = std::move(borrowed);
                                }
                                restore_storage_readers(previous_readers);
                                accesses.resize(previous_accesses);
                                co_return {};
                            }
                        }
                    );
                },
                [&](const SemArrayAdopt& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto original = (co_await evaluate(*value.source, false));
                    if (!flow.normal.has_value()) {
                        co_return {};
                    }
                    const auto& storage = operand_storage;
                    const auto adopt = [&](this const auto& self,
                                           ConstructionTypeRef from,
                                           ConstructionTypeRef to,
                                           const OwnershipProjectionPath& path) noexcept -> void {
                        if (from == to) {
                            merge_relationships(
                                flow.normal->value,
                                nest_relationships(project_relationships(original, path), path)
                            );
                            return;
                        }
                        const auto target = analysis.model.shape(to);
                        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&target)) {
                            const auto input =
                                std::get<ConstructionArrayTypeValue>(analysis.model.shape(from));
                            for (auto index = 0uz; index < array->extent; ++index) {
                                auto element = path;
                                element.push_back(index);
                                self(input.element, array->element, element);
                            }
                            return;
                        }
                        const auto source_type = analysis.model.concrete_type(from);
                        const auto target_type = analysis.model.concrete_type(to);
                        if (!source_type || !target_type) {
                            invariant_violation(
                                "callable adoption requires a concrete leaf contract"
                            );
                        }
                        const auto adaptation =
                            callable_adaptation(program, *source_type, *target_type);
                        if (!adaptation.borrows_storage()) {
                            flow.normal->value.edit().callable_loans.push_back(
                                OwnershipCallableLoan {
                                    .holder = path,
                                    .backing = std::nullopt,
                                    .callable = *adaptation.callable,
                                    .origin = source.origin,
                                    .direct_only = false
                                }
                            );
                            return;
                        }
                        if (storage.empty()) {
                            diagnose(
                                DiagnosticCode::TypeCallableViewEscape,
                                "an indirect target cannot establish a Carven callable borrow",
                                source.origin
                            );
                            merge_relationships(
                                flow.normal->value,
                                nest_relationships(opaque_callable_targets(to, source.origin), path)
                            );
                            return;
                        }
                        for (auto element : storage) {
                            element.path.insert(element.path.end(), path.begin(), path.end());
                            flow.normal->value.edit().callable_loans.push_back(
                                OwnershipCallableLoan {
                                    .holder = path,
                                    .backing = element,
                                    .callable = adaptation.callable,
                                    .origin = source.origin,
                                    .direct_only = full_expression_storage(element.object)
                                }
                            );
                        }
                    };
                    if (!original.view().storage_loans.empty()) {
                        flow.normal->value.edit().storage_loans = original.view().storage_loans;
                    } else if (auto* rows = flow.normal->value.edit_existing()) {
                        rows->storage_loans.clear();
                    }
                    adopt(value.source->type.construction(), source.type.construction(), {});
                    co_return {};
                },
                [&](const SemBorrowCallable& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto adaptation = callable_adaptation(
                        program,
                        value.source->type.resolved(),
                        source.type.resolved()
                    );
                    if (std::holds_alternative<SemTake>(value.source->value)
                        && analysis.contents(value.source->type.construction())
                               .contains_callable_view
                        && adaptation.borrows_storage()) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "taken callable storage cannot back a widened view",
                            source.origin
                        );
                    }
                    const auto relationships = (co_await evaluate(*value.source, false));
                    const auto& backing = operand_storage;
                    if (!flow.normal.has_value()) {
                        co_return {};
                    }
                    flow.normal->value = relationships;
                    // Equal view types copy the target description. They do not
                    // borrow the intermediate view's storage.
                    if (adaptation.kind == CallableAdaptationKind::CopyTarget) {
                        co_return {};
                    }
                    if (adaptation.borrows_storage() && backing.empty()) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "an indirect target cannot establish a Carven callable borrow",
                            source.origin
                        );
                        flow.normal->value =
                            opaque_callable_targets(source.type.construction(), source.origin);
                        co_return {};
                    }
                    auto storage = std::vector<OwnershipStorageLoan>();
                    if (auto* rows = flow.normal->value.edit_existing()) {
                        storage = std::move(rows->storage_loans);
                    }
                    if (!adaptation.borrows_storage()) {
                        flow.normal->value = OwnershipRelationships(
                            OwnershipRelationshipRows {
                                .callable_loans = {OwnershipCallableLoan {
                                    .holder = {},
                                    .backing = std::nullopt,
                                    .callable = *adaptation.callable,
                                    .origin = source.origin,
                                    .direct_only = false
                                }},
                                .captures = {},
                                .storage_loans = std::move(storage),
                                .formal_bundles = {}
                            }
                        );
                    } else {
                        flow.normal->value = OwnershipRelationships(
                            OwnershipRelationshipRows {
                                .callable_loans = {},
                                .captures = {},
                                .storage_loans = std::move(storage),
                                .formal_bundles = {}
                            }
                        );
                        for (const auto& selected : backing) {
                            flow.normal->value.edit().callable_loans.push_back(
                                OwnershipCallableLoan {
                                    .holder = {},
                                    .backing = selected,
                                    .callable = adaptation.callable,
                                    .origin = source.origin,
                                    .direct_only = full_expression_storage(selected.object)
                                        && analysis.contents(value.source->type.construction())
                                               .contains_closure_owner
                                }
                            );
                        }
                    }
                    co_return {};
                },
                [&](const SemTake& value) noexcept -> ContinuationTask<std::monostate> {
                    flow = (co_await place(*value.place, std::move(flow.normal->state), true));
                    if (!flow.normal.has_value()) {
                        co_return {};
                    }
                    const auto target = flow.normal->storage.front();
                    const auto effect = OwnershipWriteEffect {
                        .place = target,
                        .invalidates = true,
                        .storage = true,
                        .origin = source.origin,
                        .take = true,
                        .preserves_backing = false
                    };
                    if (!std::ranges::contains(effects, effect)) {
                        effects.push_back(effect);
                    }
                    if (diagnosing) {
                        if (const auto conflict = take_conflict(flow.normal->state, target)) {
                            diagnose(
                                conflict->code,
                                std::string(conflict->message),
                                source.origin,
                                conflict->related
                            );
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
                            auto selected = (co_await place(
                                capture.expression,
                                std::move(flow.normal->state),
                                true
                            ));
                            flow.normal = std::move(selected.normal);
                            append_ownership_exits(flow, selected);
                            if (!flow.normal.has_value()) {
                                break;
                            }
                            flow.normal->value = std::move(accumulated);
                            for (const auto& target : flow.normal->storage) {
                                write_access(target, capture.expression.origin, false);
                                flow.normal->value.edit().captures.push_back(
                                    OwnershipCapture {
                                        .holder = OwnershipProjectionPath {index},
                                        .target = target,
                                        .origin = source.origin
                                    }
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
                [&](const SemColdCall& value) noexcept -> ContinuationTask<std::monostate> {
                    static_cast<void>((co_await evaluate(*value.callee, false)));
                    // Only runtime arguments are retained by the cold activation.
                    const auto& signature = program.callable_signatures().signature(
                        program.declarations().callable(value.target).signature
                    );
                    co_await retain_cold_arguments(value.arguments, signature.parameters);
                    co_return {};
                },
                [&](const SemColdCompose& value) noexcept -> ContinuationTask<std::monostate> {
                    for (const auto& input : value.inputs) {
                        const auto retained = co_await evaluate(input.expression, true);
                        if (!flow.normal) {
                            break;
                        }
                        merge_relationships(flow.normal->value, retained);
                    }

                    co_return {};
                },
                [&](const SemColdTimeout& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto retained = co_await evaluate(*value.operation, true);
                    if (flow.normal) {
                        merge_relationships(flow.normal->value, retained);
                        static_cast<void>(co_await evaluate(*value.duration_ticks, false));
                    }
                    co_return {};
                },
                [&](const SemAwait& value) noexcept -> ContinuationTask<std::monostate> {
                    static_cast<void>((co_await evaluate(*value.operand, false)));
                    if (!flow.normal) {
                        co_return {};
                    }
                    if (value.operand_kind == AsyncAwaitOperandKind::LexicalChild) {
                        const auto* binding = std::get_if<SemBinding>(&value.operand->value);
                        if (binding == nullptr) {
                            invariant_violation("child observation lacks canonical binding");
                        }
                        const auto place = binding_place(binding->binding);
                        require_available(flow.normal->state, place, source.origin);
                        auto& child = flow.normal->state.objects[place.object];
                        child.available = false;
                        child.modified = true;
                        if (!child.taken) {
                            child.taken = source.origin;
                        }
                        child.child_intent = true;
                        child.relationships = {};
                    } else {
                        // Await completion closes consumed activation temporaries.
                        visit_semantic_nodes(
                            *value.operand,
                            [&](const SemanticExpression& operand) noexcept {
                                if (!analysis.contents(operand.type.construction())
                                         .contains_operation_owner) {
                                    return;
                                }
                                if (const auto found =
                                        facts.temporaries.find(std::addressof(operand));
                                    found != facts.temporaries.end()) {
                                    auto& temporary =
                                        flow.normal->state
                                            .objects[input.objects.size() + found->second];
                                    temporary.relationships = {};
                                    temporary.available = false;
                                }
                            }
                        );
                    }
                    flow.normal->value = {};
                    const auto* operation = std::get_if<ConstructionOperationTypeValue>(
                        &analysis.model.shape(value.operand->type.construction())
                    );
                    if (operation == nullptr) {
                        invariant_violation("await ownership requires an operation contract");
                    }
                    // Operand failures already retain their evaluation states.
                    append_failures(flow, operation->failures, flow.normal->state);
                    flow.exits.push_back(
                        OwnershipExit {
                            .payload = OwnershipCancelled {},
                            .state = flow.normal->state
                        }
                    );
                    co_return {};
                },
                [&](const SemWaitReady& value) noexcept -> ContinuationTask<std::monostate> {
                    for (const auto binding : value.children) {
                        require_available(
                            flow.normal->state,
                            binding_place(binding),
                            source.origin
                        );
                    }
                    // Waiting neither consumes the child nor ends its retained loans.
                    flow.normal->value = {};
                    co_return {};
                },
                [&](const SemCloseChild& value) noexcept -> ContinuationTask<std::monostate> {
                    auto& child = flow.normal->state.objects[binding_place(value.child).object];
                    // A branch may already have consumed this child. Drain is idempotent.
                    child.available = false;
                    child.modified = true;
                    if (!child.taken) {
                        child.taken = source.origin;
                    }
                    child.child_intent = true;
                    child.relationships = {};
                    flow.normal->value = {};
                    co_return {};
                },
                [&](const SemAsyncIntrinsic& value) noexcept -> ContinuationTask<std::monostate> {
                    if (value.kind == AsyncIntrinsic::CancelChild) {
                        if (!value.child) {
                            invariant_violation("child cancellation lacks canonical binding");
                        }
                        const auto place = binding_place(*value.child);
                        require_available(flow.normal->state, place, source.origin);
                        // Request alone retains observation and backing.
                        flow.normal->state.objects[place.object].child_intent = true;
                    }
                    co_return {};
                },
                [&](const SemGenericCall& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous = accesses.size();
                    const auto previous_readers = storage_readers.size();
                    auto arguments = std::vector<OwnershipCallArgument>();
                    const auto& definition = analysis.model.generics.definition(value.definition);
                    const auto* function = std::get_if<GenericFunctionDefinition>(&definition);
                    if (!function) {
                        invariant_violation("generic call lacks a function contract");
                    }
                    const auto cold = function->signature.execution == CallableExecutionKind::Async;
                    if (cold) {
                        co_await retain_cold_arguments(
                            value.arguments,
                            function->signature.parameters
                        );
                    } else {
                        for (const auto& argument : value.arguments) {
                            auto prepared = co_await prepare_call_argument(argument, true);
                            if (!prepared) {
                                break;
                            }
                            arguments.push_back(std::move(*prepared));
                        }
                    }
                    if (flow.normal && !cold) {
                        auto invoked = call_body(
                            value.definition,
                            {},
                            std::nullopt,
                            arguments,
                            std::move(flow.normal->state),
                            source.origin,
                            value.callee_failures.reference()
                        );
                        flow.normal = std::move(invoked.normal);
                        append_ownership_exits(flow, invoked);
                    }
                    accesses.resize(previous);
                    restore_storage_readers(previous_readers);
                    co_return {};
                },
                [&](const SemCall& value) noexcept -> ContinuationTask<std::monostate> {
                    const auto previous = accesses.size();
                    const auto previous_readers = storage_readers.size();
                    const auto concrete = callable_identity(program, value.callee->type.resolved());
                    const auto body_known =
                        concrete && program.declarations().body_for_callable(*concrete).has_value();
                    auto callee = OwnershipRelationships {};
                    auto callee_storage = std::vector<OwnershipPlace>();
                    if (concrete.has_value()
                        && value.callee->category == SemanticValueCategory::Place) {
                        auto located =
                            (co_await place(*value.callee, std::move(flow.normal->state), true));
                        flow.normal = std::move(located.normal);
                        append_ownership_exits(flow, located);
                        callee =
                            flow.normal ? std::move(flow.normal->value) : OwnershipRelationships {};
                        if (flow.normal) {
                            callee_storage = flow.normal->storage;
                        }
                    } else {
                        callee = (co_await evaluate(*value.callee, false));
                        callee_storage = operand_storage;
                    }
                    if (concrete.has_value()) {
                        for (const auto& selected : callee_storage) {
                            accesses.push_back(
                                OwnershipAccess {
                                    .place = selected,
                                    .kind = selected_access_kind(*value.callee),
                                    .descendants = false
                                }
                            );
                        }
                    }
                    protect(callee, true);
                    auto parameters = std::vector<OwnershipCallArgument>();
                    for (const auto& argument : value.arguments) {
                        auto prepared = co_await prepare_call_argument(argument, body_known);
                        if (!prepared) {
                            break;
                        }
                        parameters.push_back(std::move(*prepared));
                    }
                    if (flow.normal.has_value()) {
                        auto invoked = OwnershipFlow {.normal = {}, .exits = {}};
                        const auto invoke = [&](
                                                this const auto& self,
                                                const OwnershipRelationships& target,
                                                std::optional<CallableID> function,
                                                std::optional<OwnershipPlace> capture_owner
                                            ) noexcept -> ContinuationTask<std::monostate> {
                            use(target, flow.normal->state, source.origin, true);
                            const auto opaque = std::ranges::any_of(
                                target.view().callable_loans,
                                [](const auto& loan) static noexcept {
                                    return !loan.backing && !loan.callable;
                                }
                            );
                            if (function.has_value()
                                && (!opaque || !target.view().captures.empty())) {
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
                                if (!opaque) {
                                    co_return {};
                                }
                            }
                            for (const auto& loan : target.view().callable_loans) {
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
                                        OwnershipRelationships(
                                            OwnershipRelationshipRows {
                                                .callable_loans = {},
                                                .captures = {},
                                                .storage_loans = target.view().storage_loans,
                                                .formal_bundles = target.view().formal_bundles
                                            }
                                        )
                                    );
                                    (co_await self(backing, loan.callable, loan.backing));
                                } else if (loan.callable.has_value()) {
                                    (co_await self({}, loan.callable, std::nullopt));
                                } else {
                                    const auto signature = [&]() noexcept {
                                        if (function) {
                                            return program.declarations()
                                                .callable(*function)
                                                .signature;
                                        }
                                        const auto* view = std::get_if<CallableViewTypeValue>(
                                            &program.types()
                                                 .type(value.callee->type.resolved())
                                                 .value
                                        );
                                        if (!view) {
                                            invariant_violation(
                                                "opaque call requires a callable view"
                                            );
                                        }
                                        return view->signature;
                                    }();
                                    auto next = call_contract(
                                        program.callable_signatures().signature(signature),
                                        parameters,
                                        flow.normal->state,
                                        source.origin
                                    );
                                    join_normal_ownership(invoked.normal, next.normal);
                                    append_ownership_exits(invoked, next);
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
        finish_expression(source, *flow.normal, direct);
    }
    co_return flow;
}
