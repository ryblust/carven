module carven:backend.realization.expr.impl;

import :backend.construction;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.expr;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.format;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

BodyRealizer::ExpressionBuilder::ExpressionBuilder(
    BodyRealizer& owner,
    ConstructionExpressionID source
) noexcept
    : owner(owner),
      cleanup(owner.construction.expression(source).lifetime),
      previous_frame(std::exchange(owner.active_frame, this)) {
    static_cast<void>(owner.metadata.lifetime_regions().region(cleanup));
}

BodyRealizer::ExpressionBuilder::~ExpressionBuilder() noexcept {
    owner.active_frame = previous_frame;
}

auto BodyRealizer::ExpressionBuilder::owns(ConstructionExpressionID source) const noexcept -> bool {
    return owner.construction.expression(source).lifetime == cleanup;
}

auto BodyRealizer::ExpressionBuilder::evaluate(
    ConstructionExpressionID source,
    ConstantLiteralContext literal,
    ResultDemand demand,
    ConstructionUse use
) noexcept -> Lowered<LoweringResult> {
    auto outer = std::move(statements);
    statements = LoweringStmtBuilder();
    auto result = finish(source, literal, demand, use, true);
    statements = std::move(outer);
    return result;
}

auto BodyRealizer::ExpressionBuilder::deliver_structured(
    ConstructionExpressionID source,
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
    ConstructionExpressionID source,
    ConstantLiteralContext literal,
    ResultDemand demand,
    ConstructionUse final_use,
    bool shared
) noexcept -> Lowered<LoweringResult> {
    auto recipe = build(
        source,
        nullptr,
        demand != ResultDemand::Discard,
        final_use,
        !shared,
        demand == ResultDemand::PropagateOutcome
    );
    if (!statements.continues()) {
        return take_statements(shared).complete<LoweringResult>(std::nullopt);
    }
    if (std::holds_alternative<LoweringCompleted>(recipe.completion)) {
        return take_statements(shared).complete<LoweringResult>(LoweringCompleted {});
    }
    if (demand == ResultDemand::Discard) {
        discard_pending(recipe);
        return take_statements(shared).complete<LoweringResult>(LoweringCompleted {});
    }
    if (demand == ResultDemand::PropagateOutcome) {
        return take_statements(shared).complete<LoweringResult>(
            LoweringDirectExpression {std::get<TargetExpr>(std::move(recipe.completion))}
        );
    }
    // A residual expression may be consumed after an intervening C++ statement.
    // Its borrowed backing belongs to this source frame, not that statement.
    preserve_borrows(recipe);
    auto result = emit(recipe, final_use, literal);
    return take_statements(shared).complete<LoweringResult>(
        LoweringDirectExpression {std::move(result)}
    );
}

auto BodyRealizer::ExpressionBuilder::initialize(
    const ConstructionInitialize& initialization,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto recipe = build(initialization.initializer, nullptr, true, ConstructionUse::Consume, true);
    if (!statements.continues()) {
        destination.scope(take_statements());
        return;
    }
    auto value = emit(recipe, ConstructionUse::Consume, ConstantLiteralContext::TargetTyped);
    if (statements.empty()) {
        owner.declare_binding(initialization.binding, std::move(value), destination);
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
    const ConstructionAssign& assignment,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto target = build(assignment.target, nullptr, true, ConstructionUse::Place);
    if (!statements.continues()) {
        destination.scope(take_statements());
        return;
    }
    if (!std::holds_alternative<SemBinding>(source(target).operation.value)
        && (source(target).requires_execution
            || owner.construction.expression(assignment.value).requires_execution)) {
        // Even an otherwise pure dereference selects a place before the RHS
        // may rebind its pointer slot. Do not use the value-read anchor gate.
        preserve_borrows(target);
        const auto name = owner.names.fresh(TargetTemporaryNameKind::Owner);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::RvalueReference,
                .maybe_unused = false,
                .name = name,
                .type = owner.context.intrinsic_type(TargetSymbol::Auto),
                .initializer = raw(target)
            }
        ));
        complete(target, Saved {.name = name, .kind = SavedKind::Place});
    }
    const auto target_type = source(target).type;
    const auto external = std::holds_alternative<CppTypeValue>(
                              owner.context.semantic().types().type(target_type).value
                          )
        || std::holds_alternative<CppTypeValue>(
                              owner.context.semantic()
                                  .types()
                                  .type(owner.construction.expression(assignment.value).type)
                                  .value
        );
    auto previous = std::optional<TargetIdentifier>();
    if (assignment.compound && !external
        && owner.construction.expression(assignment.value).requires_execution) {
        const auto name = owner.names.fresh(TargetTemporaryNameKind::Operand);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = name,
                .type = owner.context.lower_type(target_type),
                .initializer = raw(target)
            }
        ));
        previous = name;
    }
    auto right = build(assignment.value, nullptr, true, ConstructionUse::Consume, true);
    if (statements.continues()) {
        auto value = emit(right, ConstructionUse::Consume, ConstantLiteralContext::Exact);
        auto operation = TargetAssignmentOperator::Assign;
        if (assignment.compound && !external) {
            value = realize_binary(
                owner.context,
                previous ? name_expression(*previous) : raw(target),
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
            TargetAssignmentStmt {.target = raw(target), .op = operation, .value = std::move(value)}
        ));
    }
    destination.scope(take_statements());
}

auto BodyRealizer::ExpressionBuilder::saved(const Recipe& recipe) const noexcept -> const Saved* {
    return std::get_if<Saved>(&recipe.completion);
}

auto BodyRealizer::ExpressionBuilder::source(const Recipe& recipe) const noexcept
    -> const ConstructionExpression& {
    return owner.construction.expression(recipe.expression_id);
}

auto BodyRealizer::ExpressionBuilder::scalar(TypeID id) const noexcept -> bool {
    const auto& type = owner.context.semantic().types().type(id).value;
    const auto* builtin = std::get_if<BuiltinTypeValue>(&type);
    return (builtin != nullptr && builtin->kind != BuiltinType::String)
        || std::holds_alternative<PointerTypeValue>(type);
}

auto BodyRealizer::ExpressionBuilder::names_storage(
    const ConstructionExpression& value
) const noexcept -> bool {
    return value.operation.selects_storage();
}

auto BodyRealizer::ExpressionBuilder::pending(const Recipe& recipe) const noexcept -> bool {
    return std::holds_alternative<std::monostate>(recipe.completion)
        || std::holds_alternative<TargetExpr>(recipe.completion);
}

auto BodyRealizer::ExpressionBuilder::build(
    ConstructionExpressionID id,
    PendingOperation* pending,
    bool result_needed,
    ConstructionUse result_use,
    bool full_expression_root,
    bool propagate_outcome
) noexcept -> Recipe {
    const auto& value = owner.construction.expression(id);
    auto recipe =
        Recipe {.expression_id = id, .inputs = {}, .operands = {}, .completion = std::monostate {}};
    if (value.constant && !value.requires_execution && scalar(value.type)) {
        return recipe;
    }
    if (std::holds_alternative<SemBinding>(value.operation.value)) {
        return recipe;
    }
    if (const auto* report = std::get_if<ConstructionTestReport>(&value.value)) {
        flush_pending(pending);
        owner.lower_report(*report, value.origin, statements);
        complete(recipe, LoweringCompleted {});
        return recipe;
    }
    if (std::holds_alternative<ConstructionConditional>(value.value)
        || std::holds_alternative<ConstructionMatch>(value.value)
        || std::holds_alternative<ConstructionTry>(value.value)) {
        flush_pending(pending);
        if (!result_needed) {
            owner.structured_expression(id, LoweringDiscardResult {}, statements);
            complete(recipe, LoweringCompleted {});
            return recipe;
        }
        if (!value.exits_test
            && owner.context.semantic()
                   .failure_sets()
                   .failure_set(value.failures)
                   .members.empty()) {
            const auto yield = owner.exit_target(LoweringExitKind::Value);
            auto result = LoweringStmtBuilder();
            owner.structured_expression(id, LoweringYieldResult {.target = yield}, result);
            complete(
                recipe,
                std::move(result).result_region(owner.context.lower_type(value.type), yield)
            );
            return recipe;
        }
        const auto storage = LoweringDeferredStorage {
            .name = owner.names.fresh(TargetTemporaryNameKind::Owner),
            .value_type = owner.context.lower_type(value.type)
        };
        auto previous_declarations = std::move(declarations);
        declarations = LoweringStmtBuilder();
        owner.structured_expression(id, LoweringInitializeResult {.storage = storage}, statements);
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
        return recipe;
    }
    if (const auto* logic = std::get_if<ConstructionShortCircuit>(&value.value)) {
        const auto constant = owner.construction.expression(logic->condition).constant;
        const auto* known = constant
            ? std::get_if<BooleanConstant>(
                  &owner.context.semantic().constants().constant(*constant).value
              )
            : nullptr;
        auto condition =
            build(logic->condition, pending, known == nullptr, ConstructionUse::OperandValue);
        if (!statements.continues()) {
            return recipe;
        }
        flush_pending(pending);
        if (known != nullptr) {
            discard_pending(condition);
            if (known->value == (logic->operation == ShortCircuitOperator::And)) {
                return build(
                    logic->selected,
                    nullptr,
                    result_needed,
                    ConstructionUse::OperandValue
                );
            }
            if (result_needed) {
                complete(recipe, bool_expression(known->value));
            } else {
                complete(recipe, LoweringCompleted {});
            }
            return recipe;
        }
        auto name = std::optional<TargetIdentifier>();
        if (result_needed) {
            name = owner.names.fresh(TargetTemporaryNameKind::Operand);
            declarations.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .name = *name,
                    .type = owner.context.lower_type(value.type),
                    .initializer = bool_expression(logic->operation == ShortCircuitOperator::Or)
                }
            ));
        }
        preserve_borrows(condition);
        auto test = emit(condition, ConstructionUse::OperandValue);
        if (logic->operation == ShortCircuitOperator::Or) {
            test = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(test));
        }
        auto previous = std::move(statements);
        statements = LoweringStmtBuilder();
        auto selected =
            build(logic->selected, nullptr, result_needed, ConstructionUse::OperandValue);
        if (statements.continues()) {
            preserve_borrows(selected);
            if (result_needed) {
                statements.emit(generated_statement(
                    TargetAssignmentStmt {
                        .target = name_expression(*name),
                        .op = TargetAssignmentOperator::Assign,
                        .value = emit(selected, ConstructionUse::OperandValue)
                    }
                ));
            } else {
                discard_pending(selected);
            }
        }
        previous.record_exits(statements.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = std::move(test), .body = std::move(statements).finish()});
        statements = std::move(previous);
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
        if (name) {
            complete(recipe, Saved {.name = *name, .kind = SavedKind::Value});
        } else {
            complete(recipe, LoweringCompleted {});
        }
        return recipe;
    }
    recipe.inputs = construction_operands(value);
    if (std::ranges::none_of(recipe.inputs, [](const auto& input) static noexcept {
            return input.demand == ConstructionDemand::Value;
        })) {
        // Complete source effects before constructing a result with no native inputs.
        if (!recipe.inputs.empty()) {
            flush_pending(pending);
        }
        for (const auto& input : recipe.inputs) {
            auto child = build(input.expression, nullptr, false, input.use);
            if (!statements.continues()) {
                return recipe;
            }
            preserve_borrows(child);
            discard_pending(child);
        }
        recipe.inputs = {};
        return recipe;
    }
    const auto& inputs = recipe.inputs;
    recipe.operands.reserve(inputs.size());
    const auto needs_order = unordered(value);
    auto current = PendingOperation {
        .previous = pending,
        .recipe = recipe,
        .postfix_end = first_unsequenced(value),
        .direct_scalars = previous_frame == nullptr
            && owner.metadata.lifetime_regions().region(cleanup).kind
                == LifetimeRegionKind::FullExpression,
        .effects = {},
        .reads = {}
    };
    for (auto index = 0uz; index < inputs.size(); ++index) {
        const auto keep = inputs[index].demand == ConstructionDemand::Value;
        auto child = build(
            inputs[index].expression,
            std::addressof(current),
            keep && (result_needed || value.executes_operation),
            inputs[index].use
        );
        if (!statements.continues()) {
            return recipe;
        }
        if (!keep) {
            // Removing an unused value keeps its effects at the original
            // position, including snapshots required before those effects.
            if (has_effect(child)) {
                flush_pending(std::addressof(current));
            }
            preserve_borrows(child);
            discard_pending(child);
        }
        // Each predecessor enters one source-ordered queue and leaves it
        // once. A later read commits effects; a later effect also commits
        // reads. Selected short-circuit paths keep their own execution.
        if (needs_order && (has_effect(child) || has_storage_read(child))) {
            commit_predecessors(current, has_effect(child));
        }
        if (index >= current.postfix_end) {
            if (has_effect(child)) {
                current.effects.push_back(index);
            } else if (has_storage_read(child)) {
                current.reads.push_back(index);
            }
        }
        recipe.operands.push_back(std::move(child));
    }
    const auto needs_stable_source = std::holds_alternative<SemArrayAdopt>(value.operation.value)
        || (std::holds_alternative<SemBorrowCallable>(value.operation.value)
            && inputs.front().use == ConstructionUse::ConstPlace);
    if (needs_stable_source && (result_needed || value.executes_operation)) {
        flush_pending(pending);
        anchor(recipe.operands.front(), inputs.front().use, true);
    }
    if (const auto* operation = std::get_if<ConstructionOperation>(&value.value);
        operation != nullptr && operation->failure) {
        flush_pending(pending);
        preserve_borrows(recipe);
        complete_call(
            recipe,
            *operation->failure,
            result_needed && !owner.context.is_void(value.type),
            result_use,
            full_expression_root
                && previous_frame == nullptr
                && owner.metadata.lifetime_regions().region(cleanup).kind
                    == LifetimeRegionKind::FullExpression
                && declarations.empty()
                && !statements.owns_storage(),
            propagate_outcome
        );
    }
    return recipe;
}

auto BodyRealizer::expression(
    ConstructionExpressionID source,
    ConstantLiteralContext literal,
    ResultDemand demand
) noexcept -> Lowered<LoweringResult> {
    if (active_frame != nullptr && active_frame->owns(source)) {
        return active_frame->evaluate(
            source,
            literal,
            demand,
            demand == ResultDemand::Observe ? ConstructionUse::ReadBorrow : ConstructionUse::Consume
        );
    }
    return ExpressionBuilder(*this, source)
        .finish(
            source,
            literal,
            demand,
            demand == ResultDemand::Observe ? ConstructionUse::ReadBorrow : ConstructionUse::Consume
        );
}

auto BodyRealizer::operand(ConstructionOperand source, ConstantLiteralContext literal) noexcept
    -> Lowered<TargetExpr> {
    auto result = active_frame != nullptr && active_frame->owns(source.expression)
        ? active_frame->evaluate(source.expression, literal, ResultDemand::Value, source.use)
        : ExpressionBuilder(*this, source.expression)
              .finish(source.expression, literal, ResultDemand::Value, source.use);
    auto statements = LoweringStmtBuilder();
    auto value = statements.accept(std::move(result));
    return std::move(statements)
        .complete<TargetExpr>(
            value ? std::optional(require_expression(std::move(*value))) : std::nullopt
        );
}

auto BodyRealizer::discard(ConstructionExpressionID source) noexcept -> Lowered<LoweringCompleted> {
    auto statements = LoweringStmtBuilder();
    if (construction.expression(source).requires_execution) {
        static_cast<void>(statements.accept(
            expression(source, ConstantLiteralContext::Exact, ResultDemand::Discard)
        ));
    }
    const auto normal = statements.continues() ? std::optional(LoweringCompleted {}) : std::nullopt;
    return std::move(statements).complete<LoweringCompleted>(normal);
}

auto BodyRealizer::condition(ConstructionExpressionID source) noexcept
    -> Lowered<LoweringPredicate> {
    const auto& source_value = construction.expression(source);
    if (source_value.constant && !source_value.requires_execution) {
        if (const auto* known = std::get_if<BooleanConstant>(
                &context.semantic().constants().constant(*source_value.constant).value
            )) {
            return LoweringStmtBuilder().complete<LoweringPredicate>(
                LoweringKnownBool {known->value}
            );
        }
    }
    const auto shared = active_frame != nullptr && active_frame->owns(source);
    auto statements = LoweringStmtBuilder();
    auto value =
        statements.accept(expression(source, ConstantLiteralContext::Exact, ResultDemand::Observe));
    if (!value) {
        return std::move(statements).complete<LoweringPredicate>(std::nullopt);
    }
    if (const auto constant = construction.expression(source).constant) {
        if (const auto* known = std::get_if<BooleanConstant>(
                &context.semantic().constants().constant(*constant).value
            )) {
            if (auto evaluation = remaining_expression(std::move(*value))) {
                statements.emit(
                    generated_statement(TargetDiscardStmt {.expression = std::move(*evaluation)})
                );
            }
            if (!shared) {
                auto completed = LoweringStmtBuilder();
                completed.scope(std::move(statements));
                return std::move(completed).complete<LoweringPredicate>(
                    LoweringKnownBool {known->value}
                );
            }
            return std::move(statements)
                .complete<LoweringPredicate>(LoweringKnownBool {known->value});
        }
    }
    auto expression_value = require_expression(std::move(*value));
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
            .type = context.lower_type(construction.expression(source).type),
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
    const ConstructionInitialize& source,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto& initializer = construction.expression(source.initializer);
    if (std::holds_alternative<ConstructionConditional>(initializer.value)
        || std::holds_alternative<ConstructionMatch>(initializer.value)
        || std::holds_alternative<ConstructionTry>(initializer.value)) {
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

auto BodyRealizer::assign(
    const ConstructionAssign& source,
    LoweringStmtBuilder& destination
) noexcept -> void {
    ExpressionBuilder(*this, source.value).assign(source, destination);
}

auto BodyRealizer::structured_delivery(
    ConstructionExpressionID source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (active_frame != nullptr && active_frame->owns(source)) {
        active_frame->deliver_structured(source, result, destination, true);
        return;
    }
    ExpressionBuilder(*this, source).deliver_structured(source, result, destination, false);
}
