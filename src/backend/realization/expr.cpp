module carven:backend.realization.expr.impl;

import :backend.preparation.body;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.expr;
import :backend.realization.format;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.realization.report;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.delegation;
import :semantic.semir.format;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

BodyRealizer::ExpressionBuilder::ExpressionBuilder(
    BodyRealizer& owner,
    const SemanticExpression& source
) noexcept
    : owner(owner),
      cleanup(owner.preparation.operation(source).operation.lifetime),
      previous_frame(std::exchange(owner.active_frame, this)) {
    static_cast<void>(owner.metadata.lifetime_regions().region(cleanup));
}

BodyRealizer::ExpressionBuilder::~ExpressionBuilder() noexcept {
    owner.active_frame = previous_frame;
}

auto BodyRealizer::ExpressionBuilder::owns(const SemanticExpression& source) const noexcept
    -> bool {
    return owner.preparation.operation(source).operation.lifetime == cleanup;
}

auto BodyRealizer::ExpressionBuilder::evaluate(
    const SemanticExpression& source,
    ConstantLiteralContext literal,
    ResultDemand demand,
    PreparedUse use
) noexcept -> Lowered<LoweringResult> {
    auto outer = std::move(statements);
    statements = LoweringStmtBuilder();
    auto result = finish(source, literal, demand, use, true);
    statements = std::move(outer);
    return result;
}

auto BodyRealizer::ExpressionBuilder::deliver_structured(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination,
    bool shared
) noexcept -> void {
    auto outer = std::move(statements);
    statements = LoweringStmtBuilder();
    owner.structured_expression(source, result, statements);
    destination.append(take_statements(shared));
    statements = std::move(outer);
}

auto BodyRealizer::ExpressionBuilder::finish(
    const SemanticExpression& source,
    ConstantLiteralContext literal,
    ResultDemand demand,
    PreparedUse final_use,
    bool shared
) noexcept -> Lowered<LoweringResult> {
    auto& recipe = *(build(
                         source,
                         nullptr,
                         demand != ResultDemand::Discard,
                         final_use,
                         !shared,
                         demand == ResultDemand::PropagateOutcome
    )
                         .run());
    if (!statements.continues()) {
        return take_statements(shared).complete<LoweringResult>(std::nullopt);
    }
    if (std::holds_alternative<LoweringCompleted>(recipe.completion)) {
        return take_statements(shared).complete<LoweringResult>(LoweringCompleted {});
    }
    if (demand == ResultDemand::Discard) {
        discard_pending(recipe).run();
        return take_statements(shared).complete<LoweringResult>(LoweringCompleted {});
    }
    if (demand == ResultDemand::PropagateOutcome) {
        return take_statements(shared).complete<LoweringResult>(
            LoweringDirectExpression {std::get<TargetExpr>(std::move(recipe.completion))}
        );
    }
    if (demand == ResultDemand::DirectReturn) {
        const auto* preparation =
            std::get_if<PreparedFormat>(owner.preparation.operation(source).preparation.get());
        const auto* writer =
            preparation != nullptr ? std::get_if<PreparedWriterFormat>(preparation) : nullptr;
        if (writer != nullptr) {
            const auto& format =
                std::get<SemFormat>(owner.preparation.operation(source).operation.value);
            complete_writer(
                recipe,
                format,
                *writer,
                owner.names.fresh(TargetTemporaryNameKind::Owner)
            )
                .run();
        }
    }
    // A residual expression may be consumed after an intervening C++ statement.
    // Its borrowed backing belongs to this source frame, not that statement.
    preserve_borrows(recipe).run();
    auto result = emit(recipe, final_use, literal).run();
    return take_statements(shared).complete<LoweringResult>(
        LoweringDirectExpression {std::move(result)}
    );
}

auto BodyRealizer::ExpressionBuilder::initialize(
    const SemInitialize& initialization,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto& recipe =
        *(build(initialization.initializer, nullptr, true, PreparedUse::Consume, true).run());
    if (!statements.continues()) {
        destination.scope(take_statements());
        return;
    }
    auto value = emit(recipe, PreparedUse::Consume, ConstantLiteralContext::TargetTyped).run();
    if (statements.empty() && declarations.empty()) {
        owner.declare_binding(initialization.binding, std::move(value), destination);
        return;
    }
    const auto& source = initialization.initializer;
    if (!source.exits_test
        && owner.context.semantic()
               .failure_sets()
               .failure_set(source.failures.resolved())
               .members.empty()) {
        const auto yield = owner.exit_target(LoweringExitKind::Value);
        owner.emit_return(std::move(value), statements, LoweringYieldResult {.target = yield});
        owner.declare_binding(
            initialization.binding,
            take_statements().result_region(
                owner.context.lower_type(owner.metadata.binding(initialization.binding).type),
                yield
            ),
            destination
        );
        return;
    }
    // Final storage belongs to the binding's lexical region. Pending source
    // owners belong to this full expression and die after direct delivery.
    const auto storage = LoweringDeferredStorage {
        .name = owner.binding_names.at(initialization.binding),
        .value_type = owner.context.lower_type(owner.metadata.binding(initialization.binding).type)
    };
    owner.delayed_bindings.emplace(initialization.binding, storage);
    owner.declare_deferred(storage, true, destination);
    owner.initialize_deferred(storage, std::move(value), statements);
    destination.scope(take_statements());
}

auto BodyRealizer::ExpressionBuilder::assign(
    const SemAssign& assignment,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto& target = *(build(assignment.target, nullptr, true, PreparedUse::WritePlace).run());
    if (!statements.continues()) {
        destination.scope(take_statements());
        return;
    }
    if (!std::holds_alternative<SemBinding>(source(target).operation.value)
        && (source(target).requires_execution
            || owner.preparation.operation(assignment.value).requires_execution)) {
        // Even an otherwise pure dereference selects a place before the RHS
        // may rebind its pointer slot. Do not use the value-read anchor gate.
        preserve_borrows(target).run();
        const auto name = owner.names.fresh(TargetTemporaryNameKind::Owner);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::RvalueReference,
                .maybe_unused = false,
                .name = name,
                .type = owner.context.intrinsic_type(TargetSymbol::Auto),
                .initializer = emit(target, PreparedUse::WritePlace).run()
            }
        ));
        complete(target, Saved {.name = name, .kind = SavedKind::Place});
    }
    const auto target_type = source(target).operation.type.resolved();
    const auto external =
        std::holds_alternative<CppTypeValue>(
            owner.context.semantic().types().type(target_type).value
        )
        || std::holds_alternative<CppTypeValue>(
            owner.context.semantic()
                .types()
                .type(owner.preparation.operation(assignment.value).operation.type.resolved())
                .value
        );
    auto previous = std::optional<TargetIdentifier>();
    if (assignment.compound
        && !external
        && owner.preparation.operation(assignment.value).requires_execution) {
        const auto name = owner.names.fresh(TargetTemporaryNameKind::Operand);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = name,
                .type = owner.context.lower_type(target_type),
                .initializer = raw(target).run()
            }
        ));
        previous = name;
    }
    auto& right = *(build(assignment.value, nullptr, true, PreparedUse::Consume, true).run());
    if (statements.continues()) {
        auto value = emit(right, PreparedUse::Consume, ConstantLiteralContext::Exact).run();
        auto operation = TargetAssignmentOperator::Assign;
        if (assignment.compound && !external) {
            value = realize_binary(
                owner.context,
                previous ? name_expression(*previous) : raw(target).run(),
                *assignment.compound,
                std::move(value),
                target_type
            );
        } else if (assignment.compound) {
            switch (*assignment.compound) {
                case BinaryOperator::Add: operation = TargetAssignmentOperator::Add; break;
                case BinaryOperator::Subtract:
                    operation = TargetAssignmentOperator::Subtract;
                    break;
                case BinaryOperator::Multiply:
                    operation = TargetAssignmentOperator::Multiply;
                    break;
                case BinaryOperator::Divide: operation = TargetAssignmentOperator::Divide; break;
                case BinaryOperator::Remainder:
                    operation = TargetAssignmentOperator::Remainder;
                    break;
                case BinaryOperator::BitwiseAnd:
                    operation = TargetAssignmentOperator::BitwiseAnd;
                    break;
                case BinaryOperator::BitwiseOr:
                    operation = TargetAssignmentOperator::BitwiseOr;
                    break;
                case BinaryOperator::BitwiseXor:
                    operation = TargetAssignmentOperator::BitwiseXor;
                    break;
                case BinaryOperator::LeftShift:
                    operation = TargetAssignmentOperator::LeftShift;
                    break;
                case BinaryOperator::RightShift:
                    operation = TargetAssignmentOperator::RightShift;
                    break;
                default: invariant_violation("invalid native compound assignment");
            }
        }
        statements.emit(generated_statement(
            TargetAssignmentStmt {
                .target = emit(target, PreparedUse::WritePlace).run(),
                .op = operation,
                .value = std::move(value)
            }
        ));
    }
    destination.scope(take_statements());
}

auto BodyRealizer::ExpressionBuilder::saved(const Recipe& recipe) const noexcept -> const Saved* {
    return std::get_if<Saved>(&recipe.completion);
}

auto BodyRealizer::ExpressionBuilder::source(const Recipe& recipe) const noexcept
    -> const PreparedOperation& {
    return owner.preparation.operation(*recipe.expression);
}

auto BodyRealizer::ExpressionBuilder::scalar(TypeID id) const noexcept -> bool {
    const auto& type = owner.context.semantic().types().type(id).value;
    const auto* builtin = std::get_if<BuiltinTypeValue>(&type);
    return (builtin != nullptr && builtin->kind != BuiltinType::String)
        || std::holds_alternative<PointerTypeValue>(type);
}

auto BodyRealizer::ExpressionBuilder::names_storage(const PreparedOperation& value) const noexcept
    -> bool {
    return value.operation.selects_storage();
}

auto BodyRealizer::ExpressionBuilder::pending(const Recipe& recipe) const noexcept -> bool {
    return std::holds_alternative<std::monostate>(recipe.completion)
        || std::holds_alternative<TargetExpr>(recipe.completion);
}

auto BodyRealizer::ExpressionBuilder::build(
    const SemanticExpression& expression,
    PendingOperation* pending,
    bool result_needed,
    PreparedUse result_use,
    bool full_expression_root,
    bool propagate_outcome
) noexcept -> ContinuationTask<Recipe*> {
    const auto& value = owner.preparation.operation(expression);
    if (!result_needed && value.requires_execution && borrowed_owner(value, result_use)) {
        // Structured producers must still deliver borrowed owners so their
        // cleanup remains in this frame when only effects are needed.
        result_needed = true;
    }
    auto& recipe = recipes.emplace_back(
        Recipe {
            .expression = std::addressof(expression),
            .inputs = {},
            .operands = {},
            .completion = std::monostate {}
        }
    );
    const auto observed =
        owner.test_observation && owner.test_observation->expression == std::addressof(expression);
    if (!observed
        && value.operation.constant
        && !value.requires_execution
        && scalar(value.operation.type.resolved())) {
        co_return std::addressof(recipe);
    }
    if (!observed
        && result_needed
        && value.operation.constant
        && !value.executes_operation
        && !names_storage(value)
        && scalar(value.operation.type.resolved())) {
        // A normal-completion fact replaces only the value. Complete the
        // original effects through the discard path in the same cleanup frame.
        (co_await flush_pending(pending));
        auto& effects = *((co_await build(expression, nullptr, false, PreparedUse::OperandValue)));
        if (statements.continues()) {
            (co_await discard_pending(effects));
            complete(recipe, constant_expression(owner.context, *value.operation.constant));
        } else {
            complete(recipe, LoweringCompleted {});
        }
        co_return std::addressof(recipe);
    }
    if (std::holds_alternative<SemBinding>(value.operation.value)) {
        co_return std::addressof(recipe);
    }
    if (const auto* report = std::get_if<SemTestReport>(&value.operation.value)) {
        (co_await flush_pending(pending));
        owner.lower_report(*report, value.operation.origin, statements);
        complete(recipe, LoweringCompleted {});
        co_return std::addressof(recipe);
    }
    if (std::holds_alternative<SemIf>(value.operation.value)
        || std::holds_alternative<SemMatch>(value.operation.value)
        || std::holds_alternative<SemTry>(value.operation.value)) {
        (co_await flush_pending(pending));
        if (!result_needed) {
            owner.structured_expression(expression, LoweringDiscardResult {}, statements);
            complete(recipe, LoweringCompleted {});
            co_return std::addressof(recipe);
        }
        if (!value.operation.exits_test
            && owner.context.semantic()
                   .failure_sets()
                   .failure_set(value.operation.failures.resolved())
                   .members.empty()) {
            const auto yield = owner.exit_target(LoweringExitKind::Value);
            auto result = LoweringStmtBuilder();
            owner.structured_expression(expression, LoweringYieldResult {.target = yield}, result);
            complete(
                recipe,
                std::move(result)
                    .result_region(owner.context.lower_type(value.operation.type.resolved()), yield)
            );
            co_return std::addressof(recipe);
        }
        const auto storage = LoweringDeferredStorage {
            .name = owner.names.fresh(TargetTemporaryNameKind::Owner),
            .value_type = owner.context.lower_type(value.operation.type.resolved())
        };
        auto previous_declarations = std::move(declarations);
        declarations = LoweringStmtBuilder();
        owner.structured_expression(
            expression,
            LoweringInitializeResult {.storage = storage},
            statements
        );
        if (statements.continues()) {
            // Keep final storage before its nested producers, but do not
            // allocate it when every branch exits without delivery.
            owner.declare_deferred(storage, true, previous_declarations);
            complete(recipe, Saved {.name = storage.name, .kind = SavedKind::StoredValue});
        } else {
            complete(recipe, LoweringCompleted {});
        }
        previous_declarations.append(std::move(declarations));
        declarations = std::move(previous_declarations);
        co_return std::addressof(recipe);
    }
    if (const auto* logic = std::get_if<SemShortCircuit>(&value.operation.value)) {
        if (!result_needed && !owner.preparation.operation(*logic->right).requires_execution) {
            co_return (co_await build(*logic->left, pending, false, PreparedUse::OperandValue));
        }
        const auto observe = [&](bool left, std::optional<TargetExpr> right) noexcept {
            return realize_observed_short_circuit(
                owner.context,
                owner.test_observation->writer,
                owner.test_observation->sources,
                left,
                std::move(right)
            );
        };
        const auto constant = owner.preparation.operation(*logic->left).operation.constant;
        const auto* known = constant
            ? std::get_if<BooleanConstant>(
                  &owner.context.semantic().constants().constant(*constant).value
              )
            : nullptr;
        auto& condition =
            *((co_await build(*logic->left, pending, known == nullptr, PreparedUse::OperandValue)));
        if (!statements.continues()) {
            co_return std::addressof(recipe);
        }
        (co_await flush_pending(pending));
        if (known != nullptr) {
            (co_await discard_pending(condition));
            if (known->value == (logic->operation == ShortCircuitOperator::And)) {
                auto& selected = *((
                    co_await build(*logic->right, nullptr, result_needed, PreparedUse::OperandValue)
                ));
                if (observed && statements.continues()) {
                    (co_await preserve_borrows(selected));
                    complete(
                        recipe,
                        observe(known->value, (co_await emit(selected, PreparedUse::OperandValue)))
                    );
                    co_return std::addressof(recipe);
                }
                co_return std::addressof(selected);
            }
            if (result_needed) {
                complete(
                    recipe,
                    observed && !known->value ? observe(false, std::nullopt)
                                              : bool_expression(known->value)
                );
            } else {
                complete(recipe, LoweringCompleted {});
            }
            co_return std::addressof(recipe);
        }
        (co_await preserve_borrows(condition));
        auto test = (co_await emit(condition, PreparedUse::OperandValue));
        auto previous = std::move(statements);
        statements = LoweringStmtBuilder();
        auto& selected =
            *((co_await build(*logic->right, nullptr, result_needed, PreparedUse::OperandValue)));
        auto selected_value = std::optional<TargetExpr>();
        if (statements.continues()) {
            (co_await preserve_borrows(selected));
            if (result_needed) {
                selected_value = (co_await emit(selected, PreparedUse::OperandValue));
                if (observed) {
                    selected_value = observe(
                        logic->operation == ShortCircuitOperator::And,
                        std::move(selected_value)
                    );
                }
            } else {
                (co_await discard_pending(selected));
            }
        }
        if (selected_value
            && statements.empty()
            && !(observed && logic->operation == ShortCircuitOperator::And)) {
            statements = std::move(previous);
            complete(
                recipe,
                binary_expression(
                    std::move(test),
                    logic->operation == ShortCircuitOperator::And ? TargetBinaryOperator::LogicalAnd
                                                                  : TargetBinaryOperator::LogicalOr,
                    std::move(*selected_value)
                )
            );
            co_return std::addressof(recipe);
        }
        auto name = std::optional<TargetIdentifier>();
        if (result_needed) {
            name = owner.names.fresh(TargetTemporaryNameKind::Operand);
            declarations.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .name = *name,
                    .type = owner.context.lower_type(value.operation.type.resolved()),
                    .initializer = bool_expression(logic->operation == ShortCircuitOperator::Or)
                }
            ));
            if (selected_value) {
                statements.emit(generated_statement(
                    TargetAssignmentStmt {
                        .target = name_expression(*name),
                        .op = TargetAssignmentOperator::Assign,
                        .value = std::move(*selected_value)
                    }
                ));
            }
        }
        if (logic->operation == ShortCircuitOperator::Or) {
            test = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(test));
        }
        previous.record_exits(statements.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = std::move(test), .body = std::move(statements).finish()});
        statements = std::move(previous);
        auto skipped = std::optional<std::vector<TargetStmt>>();
        if (observed && logic->operation == ShortCircuitOperator::And) {
            skipped.emplace();
            skipped->push_back(statement_expression(observe(false, std::nullopt)));
        }
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::move(skipped)}
        ));
        if (name) {
            complete(recipe, Saved {.name = *name, .kind = SavedKind::Value});
        } else {
            complete(recipe, LoweringCompleted {});
        }
        co_return std::addressof(recipe);
    }
    const auto& operands = value.operands;
    recipe.inputs.assign(operands.begin(), operands.end());
    for (auto& input : recipe.inputs) {
        if (input.use == PreparedUse::ProjectionPlace) {
            input.use =
                result_use == PreparedUse::WritePlace || result_use == PreparedUse::NativeTake
                ? PreparedUse::WritePlace
                : PreparedUse::ConstPlace;
        }
    }
    if (std::ranges::none_of(recipe.inputs, [](const auto& input) static noexcept {
            return input.demand == PreparedDemand::Value;
        })) {
        // Complete source effects before constructing a result with no native inputs.
        if (!recipe.inputs.empty()) {
            (co_await flush_pending(pending));
        }
        for (const auto& input : recipe.inputs) {
            auto& child = *((co_await build(*input.expression, nullptr, false, input.use)));
            if (!statements.continues()) {
                co_return std::addressof(recipe);
            }
            (co_await preserve_borrows(child));
            (co_await discard_pending(child));
        }
        recipe.inputs = {};
        co_return std::addressof(recipe);
    }
    const auto& inputs = recipe.inputs;
    recipe.operands.reserve(inputs.size());
    const auto suffix_begin = sequenced_suffix_begin(value);
    auto current = PendingOperation {
        .previous = pending,
        .recipe = recipe,
        .postfix_end = first_unsequenced(value),
        .direct_scalars = previous_frame == nullptr
            && owner.metadata.lifetime_regions().region(cleanup).kind
                == LifetimeRegionKind::FullExpression,
        .postfix_cursor = 0uz,
        .effects = {},
        .reads = {},
        .effect_cursor = 0uz,
        .read_cursor = 0uz,
    };
    for (auto index = 0uz; index < inputs.size(); ++index) {
        const auto keep = inputs[index].demand == PreparedDemand::Value;
        auto& child = *((co_await build(
            *inputs[index].expression,
            recipe.operands.empty() ? pending : std::addressof(current),
            keep && (result_needed || value.executes_operation),
            inputs[index].use
        )));
        if (!statements.continues()) {
            co_return std::addressof(recipe);
        }
        if (!keep) {
            // Removing an unused value keeps its effects at the original
            // position, including snapshots required before those effects.
            if (has_effect(child)) {
                (co_await flush_pending(std::addressof(current)));
            }
            (co_await preserve_borrows(child));
            (co_await discard_pending(child));
        }
        // Each predecessor enters one source-ordered queue and leaves it
        // once. A later read commits effects; a later effect also commits
        // reads. Selected short-circuit paths keep their own execution.
        if (suffix_begin != 0uz && (has_effect(child) || has_storage_read(child))) {
            (co_await commit_predecessors(current, has_effect(child), false, suffix_begin));
        }
        if (index >= current.postfix_end) {
            if (has_effect(child)) {
                current.effects.push_back(index);
            } else if (has_storage_read(child)) {
                current.reads.push_back(index);
            }
        }
        recipe.operands.push_back(std::addressof(child));
    }
    const auto* format = std::get_if<SemFormat>(&value.operation.value);
    const auto* prepared = std::get_if<PreparedFormat>(value.preparation.get());
    const auto* writer_format =
        prepared != nullptr ? std::get_if<PreparedWriterFormat>(prepared) : nullptr;
    if (format != nullptr && format->receiver && writer_format != nullptr) {
        (co_await flush_pending(pending));
        (co_await complete_writer(recipe, *format, *writer_format, std::nullopt));
        co_return std::addressof(recipe);
    }
    const auto needs_stable_source = std::holds_alternative<SemArrayAdopt>(value.operation.value)
        || (std::holds_alternative<SemBorrowCallable>(value.operation.value)
            && inputs.front().use == PreparedUse::ConstPlace);
    if (needs_stable_source && (result_needed || value.executes_operation)) {
        (co_await flush_pending(pending));
        (co_await anchor(*recipe.operands.front(), inputs.front().use, true));
    }
    if (const auto transport = owner.fallible(value.operation)) {
        (co_await flush_pending(pending));
        (co_await preserve_borrows(recipe));
        (co_await complete_call(
            recipe,
            *transport,
            result_needed && !owner.context.is_void(value.operation.type.resolved()),
            result_use,
            full_expression_root
                && previous_frame == nullptr
                && owner.metadata.lifetime_regions().region(cleanup).kind
                    == LifetimeRegionKind::FullExpression
                && declarations.empty()
                && !statements.owns_storage(),
            propagate_outcome
        ));
    }
    co_return std::addressof(recipe);
}

auto BodyRealizer::ExpressionBuilder::complete_writer(
    Recipe& recipe,
    const SemFormat& format,
    const PreparedWriterFormat& preparation,
    std::optional<TargetIdentifier> output
) noexcept -> ContinuationTask<std::monostate> {
    const auto offset = format.receiver ? 1uz : 0uz;
    // Existing conflict barriers have already captured reads before later effects.
    // Complete remaining nontrivial inputs before any reservation or write.
    for (auto index = 0uz; index < recipe.inputs.size(); ++index) {
        if (recipe.inputs[index].demand != PreparedDemand::Value) {
            continue;
        }
        auto& child = *recipe.operands[index];
        if (!std::holds_alternative<SemBinding>(source(child).operation.value)) {
            (co_await preserve_borrows(child));
            (co_await anchor(child, recipe.inputs[index].use, false, true));
        }
    }
    auto operands = std::vector<TargetExpr>();
    auto sizes = std::vector<TargetExpr>();
    for (auto index = 0uz; index < preparation.operand_indices.size(); ++index) {
        auto& child = *recipe.operands[preparation.operand_indices[index] + offset];
        operands.push_back((co_await raw(child)));
    }
    auto operand = 0uz;
    for (const auto& field : preparation.format.fields) {
        auto& child = *recipe.operands[preparation.operand_indices[operand] + offset];
        operand += writer_field_operand_count(field);
        const auto* type = std::get_if<BuiltinType>(&field);
        if (type != nullptr && (*type == BuiltinType::Str || *type == BuiltinType::String)) {
            sizes.push_back(call_member((co_await raw(child)), "size", {}));
        }
    }
    if (output) {
        const auto type = owner.context.lower_type(source(recipe).operation.type.resolved());
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = *output,
                .type = type,
                .initializer =
                    TargetExpr {.value = TargetConstructionExpr {.type = type, .initializer = {}}}
            }
        ));
    }
    auto writes = realize_writer_statements(
        owner.context,
        preparation.format,
        owner.names.fresh(TargetTemporaryNameKind::Operand),
        output ? name_expression(*output)
               : (co_await emit(*recipe.operands.front(), PreparedUse::WritePlace)),
        std::move(operands),
        std::move(sizes)
    );
    for (auto& statement : writes) {
        statements.emit(std::move(statement));
    }
    if (output) {
        complete(recipe, name_expression(*output));
    } else {
        complete(recipe, LoweringCompleted {});
    }
    co_return {};
}

auto BodyRealizer::expression(
    const SemanticExpression& source,
    ConstantLiteralContext literal,
    ResultDemand demand
) noexcept -> Lowered<LoweringResult> {
    if (active_frame != nullptr && active_frame->owns(source)) {
        return active_frame->evaluate(source, literal, demand, PreparedUse::Consume);
    }
    return ExpressionBuilder(*this, source).finish(source, literal, demand);
}

auto BodyRealizer::operand(PreparedOperand source, ConstantLiteralContext literal) noexcept
    -> Lowered<TargetExpr> {
    auto result = active_frame != nullptr && active_frame->owns(*source.expression)
        ? active_frame->evaluate(*source.expression, literal, ResultDemand::Value, source.use)
        : ExpressionBuilder(*this, *source.expression)
              .finish(*source.expression, literal, ResultDemand::Value, source.use);
    auto statements = LoweringStmtBuilder();
    auto value = statements.accept(std::move(result));
    return std::move(statements)
        .complete<TargetExpr>(
            value ? std::optional(require_expression(std::move(*value))) : std::nullopt
        );
}

auto BodyRealizer::discard(const SemanticExpression& source) noexcept
    -> Lowered<LoweringCompleted> {
    auto statements = LoweringStmtBuilder();
    if (preparation.operation(source).requires_execution) {
        static_cast<void>(statements.accept(
            expression(source, ConstantLiteralContext::Exact, ResultDemand::Discard)
        ));
    }
    const auto normal = statements.continues() ? std::optional(LoweringCompleted {}) : std::nullopt;
    return std::move(statements).complete<LoweringCompleted>(normal);
}

auto BodyRealizer::condition(const SemanticExpression& source) noexcept
    -> Lowered<LoweringPredicate> {
    const auto& source_value = preparation.operation(source);
    const auto shared = active_frame != nullptr && active_frame->owns(source);
    if (source_value.operation.constant) {
        if (const auto* known = std::get_if<BooleanConstant>(
                &context.semantic().constants().constant(*source_value.operation.constant).value
            )) {
            auto statements = LoweringStmtBuilder();
            const auto completion = statements.accept(discard(source));
            if (!shared) {
                auto completed = LoweringStmtBuilder();
                completed.scope(std::move(statements));
                statements = std::move(completed);
            }
            return std::move(statements)
                .complete<LoweringPredicate>(
                    completion ? std::optional<LoweringPredicate>(LoweringKnownBool {known->value})
                               : std::nullopt
                );
        }
    }
    auto statements = LoweringStmtBuilder();
    auto value = statements.accept(operand(
        {.expression = std::addressof(source),
         .use = PreparedUse::OperandValue,
         .demand = PreparedDemand::Value},
        ConstantLiteralContext::Exact
    ));
    if (!value) {
        return std::move(statements).complete<LoweringPredicate>(std::nullopt);
    }
    auto expression_value = std::move(*value);
    if (shared || statements.empty()) {
        return std::move(statements)
            .complete<LoweringPredicate>(LoweringDynamicBool {std::move(expression_value)});
    }
    // A statement-form condition has its own source full expression. Deliver
    // its scalar before cleanup; branch execution starts after that scope ends.
    const auto name = names.fresh(TargetTemporaryNameKind::Operand);
    auto completed = LoweringStmtBuilder();
    completed.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = name,
            .type = context.lower_type(preparation.operation(source).operation.type.resolved()),
            .initializer = bool_expression(false)
        }
    ));
    statements.emit(generated_statement(
        TargetAssignmentStmt {
            .target = name_expression(name),
            .op = TargetAssignmentOperator::Assign,
            .value = std::move(expression_value)
        }
    ));
    completed.scope(std::move(statements));
    return std::move(completed).complete<LoweringPredicate>(
        LoweringDynamicBool {name_expression(name)}
    );
}

auto BodyRealizer::initialize_binding(
    const SemInitialize& source,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto& initializer = preparation.operation(source.initializer);
    const auto escapes = source.initializer.exits_test
        || !context.semantic()
                .failure_sets()
                .failure_set(source.initializer.failures.resolved())
                .members.empty();
    if (escapes
        && (std::holds_alternative<SemIf>(initializer.operation.value)
            || std::holds_alternative<SemMatch>(initializer.operation.value)
            || std::holds_alternative<SemTry>(initializer.operation.value))) {
        const auto storage = LoweringDeferredStorage {
            .name = binding_names.at(source.binding),
            .value_type = context.lower_type(metadata.binding(source.binding).type)
        };
        delayed_bindings.emplace(source.binding, storage);
        declare_deferred(storage, true, destination);
        auto statements = LoweringStmtBuilder();
        result_expression(
            source.initializer,
            LoweringInitializeResult {.storage = storage},
            statements
        );
        destination.scope(std::move(statements));
        return;
    }
    ExpressionBuilder(*this, source.initializer).initialize(source, destination);
}

auto BodyRealizer::assign(const SemAssign& source, LoweringStmtBuilder& destination) noexcept
    -> void {
    ExpressionBuilder(*this, source.value).assign(source, destination);
}

auto BodyRealizer::structured_delivery(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (active_frame != nullptr && active_frame->owns(source)) {
        active_frame->deliver_structured(source, result, destination, true);
        return;
    }
    ExpressionBuilder(*this, source).deliver_structured(source, result, destination, false);
}
