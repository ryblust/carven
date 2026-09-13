module carven:backend.realization.expr.impl;

import :backend.construction;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.constant;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

// Execution buffers may nest while declarations remain with the source cleanup
// frame identified by LifetimeRegionID.
class BodyRealizer::ExpressionBuilder final {
public:
    ExpressionBuilder(BodyRealizer& owner, ConstructionExpressionID source) noexcept;

    ~ExpressionBuilder() noexcept;
    auto owns(ConstructionExpressionID source) const noexcept -> bool;
    auto evaluate(
        ConstructionExpressionID source,
        RealizationLiteralContext literal,
        ResultDemand demand,
        ConstructionUse use
    ) noexcept -> Lowered<LoweringResult>;
    auto deliver_structured(
        ConstructionExpressionID source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination,
        bool shared
    ) noexcept -> void;
    auto finish(
        ConstructionExpressionID source,
        RealizationLiteralContext literal,
        ResultDemand demand,
        ConstructionUse final_use = ConstructionUse::Consume,
        bool shared = false
    ) noexcept -> Lowered<LoweringResult>;
    auto initialize(
        const ConstructionInitialize& initialization,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto assign(const ConstructionAssign& assignment, LoweringStmtBuilder& destination) noexcept
        -> void;

private:
    enum class SavedKind { Value, Place, StoredValue, StoredPlace, Success };

    struct Saved final {
        TargetIdentifier name;
        SavedKind kind;
    };

    struct Recipe final {
        ConstructionExpressionID expression_id;
        std::span<const ConstructionOperand> inputs;
        std::vector<Recipe> operands;
        std::variant<std::monostate, Saved, TargetExpr, LoweringCompleted> completion;
    };

    // This synchronous chain only borrows live build() stack frames. It is not
    // retained in recipes or across source execution-region construction.
    struct PendingOperation final {
        PendingOperation* previous;
        Recipe& recipe;
        std::size_t postfix_end;
        bool direct_scalars;
        std::size_t postfix_cursor = 0;
        std::vector<std::size_t> effects;
        std::vector<std::size_t> reads;
        std::size_t effect_cursor = 0;
        std::size_t read_cursor = 0;
    };

    BodyRealizer& owner;
    LifetimeRegionID cleanup;
    ExpressionBuilder* previous_frame;
    LoweringStmtBuilder declarations;
    LoweringStmtBuilder statements;

    auto take_statements(bool shared = false) noexcept -> LoweringStmtBuilder;

    template<typename T>
        requires (
            std::same_as<T, Saved>
            || std::same_as<T, TargetExpr>
            || std::same_as<T, LoweringCompleted>
        )
    static auto complete(Recipe& recipe, T result) noexcept -> void {
        if (!std::holds_alternative<std::monostate>(recipe.completion)
            && !std::holds_alternative<TargetExpr>(recipe.completion)) {
            invariant_violation("recipe completed more than once");
        }
        recipe.completion = std::move(result);
    }

    auto saved(const Recipe& recipe) const noexcept -> const Saved*;
    auto source(const Recipe& recipe) const noexcept -> const ConstructionExpression&;
    auto scalar(TypeID id) const noexcept -> bool;
    auto names_storage(const ConstructionExpression& value) const noexcept -> bool;
    auto pending(const Recipe& recipe) const noexcept -> bool;
    auto discard_pending(Recipe& recipe) noexcept -> void;
    auto has_storage_read(const Recipe& recipe) const noexcept -> bool;
    auto has_effect(const Recipe& recipe) const noexcept -> bool;
    auto commit_postfix(PendingOperation& operation) noexcept -> void;
    auto commit_predecessors(
        PendingOperation& operation,
        bool include_reads,
        bool prefix_ready = false
    ) noexcept -> void;
    auto flush_pending(PendingOperation* operation) noexcept -> void;
    auto build(
        ConstructionExpressionID id,
        PendingOperation* pending,
        bool result_needed = true,
        ConstructionUse result_use = ConstructionUse::Consume,
        bool full_expression_root = false
    ) noexcept -> Recipe;
    auto unordered(const ConstructionExpression& value) const noexcept -> bool;
    auto first_unsequenced(const ConstructionExpression& value) const noexcept -> std::size_t;
    auto preserve_borrows(Recipe& recipe) noexcept -> void;
    auto raw(
        Recipe& recipe,
        RealizationLiteralContext literal = RealizationLiteralContext::Exact
    ) noexcept -> TargetExpr;
    auto emit(
        Recipe& recipe,
        ConstructionUse use,
        RealizationLiteralContext literal = RealizationLiteralContext::Exact
    ) noexcept -> TargetExpr;
    auto anchor(
        Recipe& recipe,
        ConstructionUse use,
        bool force = false,
        bool direct_scalar = false
    ) noexcept -> void;
    auto complete_call(
        Recipe& recipe,
        const ConstructionFallible& transport,
        bool project_success,
        ConstructionUse use,
        bool direct
    ) noexcept -> void;
};

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
    RealizationLiteralContext literal,
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
    RealizationLiteralContext literal,
    ResultDemand demand,
    ConstructionUse final_use,
    bool shared
) noexcept -> Lowered<LoweringResult> {
    auto recipe = build(source, nullptr, demand != ResultDemand::Discard, final_use, !shared);
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
    auto value = emit(recipe, ConstructionUse::Consume, RealizationLiteralContext::TargetTyped);
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
    if (!std::holds_alternative<SemBinding>(source(target).operation.value)) {
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
    if (assignment.compound && !external) {
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
        auto value = emit(right, ConstructionUse::Consume, RealizationLiteralContext::Exact);
        auto operation = TargetAssignmentOperator::Assign;
        if (assignment.compound && !external) {
            value = realize_binary(
                owner.context,
                name_expression(*previous),
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

auto BodyRealizer::ExpressionBuilder::take_statements(bool shared) noexcept -> LoweringStmtBuilder {
    if (shared) {
        return std::move(statements);
    }
    declarations.append(std::move(statements));
    return std::move(declarations);
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

auto BodyRealizer::ExpressionBuilder::discard_pending(Recipe& recipe) noexcept -> void {
    if (!pending(recipe)) {
        return;
    }
    if (!source(recipe).requires_execution) {
        complete(recipe, LoweringCompleted {});
        return;
    }
    if (source(recipe).executes_operation
        || std::holds_alternative<TargetExpr>(recipe.completion)) {
        statements.emit(generated_statement(TargetDiscardStmt {.expression = raw(recipe)}));
        complete(recipe, LoweringCompleted {});
        return;
    }
    for (auto& operand : recipe.operands) {
        discard_pending(operand);
    }
    complete(recipe, LoweringCompleted {});
}

auto BodyRealizer::ExpressionBuilder::has_storage_read(const Recipe& recipe) const noexcept
    -> bool {
    return pending(recipe) && source(recipe).reads_storage;
}

auto BodyRealizer::ExpressionBuilder::has_effect(const Recipe& recipe) const noexcept -> bool {
    return pending(recipe) && source(recipe).requires_execution;
}

auto BodyRealizer::ExpressionBuilder::commit_postfix(PendingOperation& operation) noexcept -> void {
    const auto end = std::min(operation.postfix_end, operation.recipe.operands.size());
    while (operation.postfix_cursor < end) {
        const auto index = operation.postfix_cursor++;
        anchor(
            operation.recipe.operands[index],
            operation.recipe.inputs[index].use,
            false,
            operation.direct_scalars
        );
    }
}

auto BodyRealizer::ExpressionBuilder::commit_predecessors(
    PendingOperation& operation,
    bool include_reads,
    bool prefix_ready
) noexcept -> void {
    auto began_prefix = prefix_ready;
    while (operation.effect_cursor < operation.effects.size()
           || (include_reads && operation.read_cursor < operation.reads.size())) {
        const auto effect = operation.effect_cursor < operation.effects.size()
            ? operation.effects[operation.effect_cursor]
            : std::numeric_limits<std::size_t>::max();
        const auto read = include_reads && operation.read_cursor < operation.reads.size()
            ? operation.reads[operation.read_cursor]
            : std::numeric_limits<std::size_t>::max();
        const auto index = std::min(effect, read);
        if (effect < read) {
            ++operation.effect_cursor;
        } else {
            ++operation.read_cursor;
        }
        auto& predecessor = operation.recipe.operands[index];
        if (!has_effect(predecessor) && !has_storage_read(predecessor)) {
            continue;
        }
        if (!began_prefix) {
            flush_pending(operation.previous);
            // A real argument prefix must first select its receiver/callee.
            commit_postfix(operation);
            began_prefix = true;
        }
        anchor(predecessor, operation.recipe.inputs[index].use, false, operation.direct_scalars);
    }
}

auto BodyRealizer::ExpressionBuilder::flush_pending(PendingOperation* operation) noexcept -> void {
    if (operation == nullptr) {
        return;
    }
    flush_pending(operation->previous);
    commit_postfix(*operation);
    commit_predecessors(*operation, true, true);
}

auto BodyRealizer::ExpressionBuilder::build(
    ConstructionExpressionID id,
    PendingOperation* pending,
    bool result_needed,
    ConstructionUse result_use,
    bool full_expression_root
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
    const auto& inputs = recipe.inputs;
    recipe.operands.reserve(inputs.size());
    const auto needs_order = unordered(value);
    auto current = PendingOperation {
        .previous = pending,
        .recipe = recipe,
        .postfix_end = first_unsequenced(value),
        .direct_scalars = full_expression_root
            && previous_frame == nullptr
            && owner.metadata.lifetime_regions().region(cleanup).kind
                == LifetimeRegionKind::FullExpression,
        .effects = {},
        .reads = {}
    };
    for (auto index = 0uz; index < inputs.size(); ++index) {
        auto child = build(
            inputs[index].expression,
            std::addressof(current),
            result_needed || value.executes_operation,
            inputs[index].use
        );
        if (!statements.continues()) {
            return recipe;
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
                && !statements.owns_storage()
        );
    }
    return recipe;
}

auto BodyRealizer::ExpressionBuilder::unordered(const ConstructionExpression& value) const noexcept
    -> bool {
    if (const auto* structure = std::get_if<SemStruct>(&value.operation.value)) {
        // C++ aggregate initialization follows declaration order. A source
        // permutation therefore uses the same actual-conflict barriers as
        // unordered call operands before the final field rearrangement.
        return !std::ranges::is_sorted(
            structure->fields,
            {},
            &SemFieldInitializer::declaration_index
        );
    }
    if (std::holds_alternative<SemArray>(value.operation.value)
        || std::holds_alternative<SemClosure>(value.operation.value)) {
        return false;
    }
    return true;
}

auto BodyRealizer::ExpressionBuilder::first_unsequenced(
    const ConstructionExpression& value
) const noexcept -> std::size_t {
    if (std::holds_alternative<SemCall>(value.operation.value)) {
        return 1uz;
    }
    if (const auto* native = std::get_if<SemCppCall>(&value.operation.value)) {
        return std::holds_alternative<CppNameReference>(native->callee) ? 0uz : 1uz;
    }
    return 0uz;
}

auto BodyRealizer::ExpressionBuilder::preserve_borrows(Recipe& recipe) noexcept -> void {
    if (!std::holds_alternative<std::monostate>(recipe.completion)
        || std::holds_alternative<SemBinding>(source(recipe).operation.value)
        || (source(recipe).constant
            && !source(recipe).requires_execution
            && scalar(source(recipe).type))) {
        return;
    }
    const auto& inputs = recipe.inputs;
    for (auto index = 0uz; index < inputs.size(); ++index) {
        auto& child = recipe.operands[index];
        const auto& value = source(child);
        if (std::holds_alternative<SemCallable>(value.operation.value)) {
            continue;
        }
        if (!scalar(value.type)
            && value.category == SemanticValueCategory::Value
            && inputs[index].use != ConstructionUse::Consume
            && inputs[index].use != ConstructionUse::NativeTake) {
            anchor(child, inputs[index].use, true);
        } else if (!saved(child) && !std::holds_alternative<SemBinding>(value.operation.value)) {
            preserve_borrows(child);
        }
    }
}

auto BodyRealizer::ExpressionBuilder::raw(
    Recipe& recipe,
    RealizationLiteralContext literal
) noexcept -> TargetExpr {
    const auto& value = source(recipe);
    if (value.constant && !value.requires_execution && scalar(value.type)) {
        return constant_expression(owner.context, *value.constant, literal);
    }
    if (std::holds_alternative<LoweringCompleted>(recipe.completion)) {
        invariant_violation("completed operation has no residual value");
    }
    if (auto* residual = std::get_if<TargetExpr>(&recipe.completion)) {
        return std::move(*residual);
    }
    if (saved(recipe)) {
        auto result = name_expression(saved(recipe)->name);
        if (saved(recipe)->kind == SavedKind::StoredValue
            || saved(recipe)->kind == SavedKind::StoredPlace
            || saved(recipe)->kind == SavedKind::Success) {
            result = dereference_expression(std::move(result));
        }
        if (saved(recipe)->kind == SavedKind::Success) {
            result = member_expression(std::move(result), TargetIdentifier::from_spelling("value"));
        }
        return result;
    }
    if (const auto* binding = std::get_if<SemBinding>(&value.operation.value)) {
        return owner.binding_expression(binding->binding);
    }
    if (const auto* constant = std::get_if<SemConstant>(&value.operation.value)) {
        return constant_expression(owner.context, constant->constant, literal);
    }
    if (std::holds_alternative<SemTake>(value.operation.value)) {
        return transfer_expression(raw(recipe.operands.front()));
    }
    if (const auto* adoption = std::get_if<SemArrayAdopt>(&value.operation.value)) {
        auto path = std::vector<std::size_t>();
        const auto projection = [&]() noexcept -> TargetExpr {
            auto input = raw(recipe.operands.front());
            for (const auto index : path) {
                input = TargetExpr {
                    .value = TargetIndexExpr {
                        .operand = target_child(std::move(input)),
                        .index = target_child(integer_expression(index))
                    }
                };
            }
            return input;
        };
        const auto adopt =
            [&](this const auto& self, TypeID from, TypeID to) noexcept -> TargetExpr {
            if (from == to) {
                return projection();
            }
            const auto& types = owner.context.semantic().types();
            if (const auto* target = std::get_if<ArrayTypeValue>(&types.type(to).value)) {
                const auto& origin = std::get<ArrayTypeValue>(types.type(from).value);
                auto elements = std::vector<TargetExpr>();
                for (auto index = 0uz; index < target->extent; ++index) {
                    path.push_back(index);
                    elements.push_back(self(origin.element, target->element));
                    path.pop_back();
                }
                return TargetExpr {
                    .value = TargetArrayExpr {
                        .element_type_id = owner.context.lower_type(target->element),
                        .extent = target_child(integer_expression(target->extent)),
                        .elements = std::move(elements)
                    }
                };
            }
            return realize_callable_adaptation(owner.context, projection(), from, to);
        };
        return adopt(adoption->source->type.resolved(), value.type);
    }

    // These checked helpers take both operands as their explicit <T>.
    const auto* binary = std::get_if<SemBinary>(&value.operation.value);
    const auto typed_arithmetic = binary != nullptr
        && owner.context.is_integer(value.type)
        && (binary->operation == BinaryOperator::Add
            || binary->operation == BinaryOperator::Subtract
            || binary->operation == BinaryOperator::Multiply
            || binary->operation == BinaryOperator::Divide
            || binary->operation == BinaryOperator::Remainder);
    const auto& inputs = recipe.inputs;
    if (recipe.operands.size() != inputs.size()) {
        invariant_violation("ordinary operation received the wrong number of operands");
    }
    auto operands = std::vector<TargetExpr>();
    operands.reserve(inputs.size());
    for (auto index = 0uz; index < inputs.size(); ++index) {
        operands.push_back(emit(
            recipe.operands[index],
            inputs[index].use,
            inputs[index].use == ConstructionUse::OperandValue
                ? (typed_arithmetic ? RealizationLiteralContext::TargetTyped : literal)
                : RealizationLiteralContext::Exact
        ));
    }
    return realize_operation(owner.context, value.operation, std::move(operands));
}

auto BodyRealizer::ExpressionBuilder::emit(
    Recipe& recipe,
    ConstructionUse use,
    RealizationLiteralContext literal
) noexcept -> TargetExpr {
    if (use == ConstructionUse::NativeTake) {
        // The query promises T&&. Do not first turn a trivial Take into
        // const T& via Carven transfer and then cast away constness.
        auto value = saved(recipe) == nullptr
                && std::holds_alternative<SemTake>(source(recipe).operation.value)
            ? raw(recipe.operands.front())
            : raw(recipe, literal);
        return TargetExpr {
            .value = TargetStaticCastExpr {
                .type =
                    owner.context
                        .reference_type(owner.context.lower_type(source(recipe).type), false, true),
                .operand = target_child(std::move(value))
            }
        };
    }
    auto result = raw(recipe, literal);
    if (use == ConstructionUse::Consume
        && saved(recipe)
        && saved(recipe)->kind != SavedKind::Place
        && saved(recipe)->kind != SavedKind::StoredPlace) {
        if (saved(recipe)->kind == SavedKind::Success && scalar(source(recipe).type)) {
            // Scalar transfer observes const T&; the success projection
            // already promises const access and cannot call transfer(T&).
            return TargetExpr {
                .value = TargetStaticCastExpr {
                    .type = owner.context.reference_type(
                        owner.context.lower_type(source(recipe).type),
                        true
                    ),
                    .operand = target_child(std::move(result))
                }
            };
        }
        return transfer_expression(std::move(result));
    }
    const auto& value = source(recipe);
    const auto copy_binding =
        (use == ConstructionUse::Consume || use == ConstructionUse::OperandValue)
        && !saved(recipe)
        && std::holds_alternative<SemBinding>(value.operation.value)
        && !scalar(value.type);
    // Named values copy even at C++ automatic-move return sites.
    if (copy_binding
        || use == ConstructionUse::ReadBorrow
        || use == ConstructionUse::ConstPlace
        || use == ConstructionUse::AddressValue) {
        const auto type = owner.context.lower_type(source(recipe).type);
        if (use == ConstructionUse::AddressValue && !saved(recipe)) {
            result = TargetExpr {
                .value =
                    TargetStaticCastExpr {.type = type, .operand = target_child(std::move(result))}
            };
        }
        result = TargetExpr {
            .value = TargetStaticCastExpr {
                .type = owner.context.reference_type(type, true),
                .operand = target_child(std::move(result))
            }
        };
    }
    return result;
}

auto BodyRealizer::ExpressionBuilder::anchor(
    Recipe& recipe,
    ConstructionUse use,
    bool force,
    bool direct_scalar
) noexcept -> void {
    if (!pending(recipe)) {
        return;
    }
    const auto& value = source(recipe);
    if (!force && !value.reads_storage && !value.requires_execution) {
        return;
    }
    if (value.category == SemanticValueCategory::Value
        && !scalar(value.type)
        && !std::holds_alternative<SemBinding>(value.operation.value)
        && value.lifetime != cleanup) {
        invariant_violation("owner anchoring requires its source cleanup frame");
    }
    if (std::holds_alternative<SemBinding>(value.operation.value)
        && (use == ConstructionUse::Place || use == ConstructionUse::ConstPlace)) {
        return;
    }
    if (!std::holds_alternative<SemBinding>(value.operation.value)) {
        preserve_borrows(recipe);
    }
    const auto name = owner.names.fresh(TargetTemporaryNameKind::Owner);
    if (direct_scalar
        && scalar(value.type)
        && use != ConstructionUse::Place
        && use != ConstructionUse::ConstPlace
        && std::holds_alternative<BuiltinTypeValue>(
            owner.context.semantic().types().type(value.type).value
        )) {
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = use == ConstructionUse::Consume || use == ConstructionUse::NativeTake
                    ? TargetVariableBinding::MutableValue
                    : TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = name,
                .type = owner.context.lower_type(value.type),
                .initializer = raw(recipe)
            }
        ));
        complete(recipe, Saved {.name = name, .kind = SavedKind::Value});
        return;
    }
    if (std::holds_alternative<SemCppCall>(value.operation.value)
        && use != ConstructionUse::Consume
        && use != ConstructionUse::NativeTake) {
        const auto* native =
            std::get_if<CppTypeValue>(&owner.context.semantic().types().type(value.type).value);
        const auto* query = native == nullptr ? nullptr : std::get_if<CppQueryType>(&native->form);
        if (query == nullptr) {
            invariant_violation("native call has no result query");
        }
        const auto exact = owner.context.target().intern_type(
            {.value = TargetDecltypeType(owner.context.cpp_type_query(*query)),
             .const_qualified = false}
        );
        const auto storage = LoweringDeferredStorage {.name = name, .value_type = exact};
        owner.declare_deferred(storage, false, declarations);
        owner.initialize_deferred(
            storage,
            raw(recipe),
            statements,
            owner.context.intrinsic_type(TargetSymbol::DecltypeAuto)
        );
        complete(recipe, Saved {.name = name, .kind = SavedKind::StoredValue});
        return;
    }
    // Consume completes a value snapshot at this barrier, not merely a
    // native invocation. Returning the normalized object type copies a
    // native T&/const T&, moves T&&, and directly constructs a prvalue.
    const auto place = (value.category == SemanticValueCategory::Place || names_storage(value))
        && (use == ConstructionUse::Place || use == ConstructionUse::ConstPlace);
    // Read describes the consumer, not ownership of a newly produced value.
    // A factory cannot extend a prvalue lifetime by returning const T&.
    const auto read = use == ConstructionUse::ReadBorrow
        && (value.category == SemanticValueCategory::Place || names_storage(value));
    const auto type = place
        ? owner.context.reference_type(
              owner.context.lower_type(value.type),
              use == ConstructionUse::ConstPlace || value.category != SemanticValueCategory::Place
          )
        : read ? owner.context.lower_parameter({.access = AccessMode::Read, .type = value.type})
               : owner.context.lower_type(value.type);
    const auto storage = LoweringDeferredStorage {.name = name, .value_type = type};
    owner.declare_deferred(storage, false, declarations);
    owner.initialize_deferred(
        storage,
        use == ConstructionUse::Consume || use == ConstructionUse::OperandValue ? emit(recipe, use)
                                                                                : raw(recipe),
        statements
    );
    complete(
        recipe,
        Saved {.name = name, .kind = place ? SavedKind::StoredPlace : SavedKind::StoredValue}
    );
}

auto BodyRealizer::ExpressionBuilder::complete_call(
    Recipe& recipe,
    const ConstructionFallible& transport,
    bool project_success,
    ConstructionUse use,
    bool direct
) noexcept -> void {
    const auto callee_type = source(recipe.operands.front()).type;
    const auto outcome = owner.names.fresh(TargetTemporaryNameKind::Outcome);
    const auto storage = LoweringDeferredStorage {
        .name = outcome,
        .value_type = owner.context.call_result(callee_type)
    };
    // All operand recipes have been prepared before choosing storage. A
    // direct root has no retained auxiliary owners or shared execution;
    // its ordinary Outcome local therefore preserves reverse destruction.
    if (direct) {
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = outcome,
                .type = storage.value_type,
                .initializer = raw(recipe)
            }
        ));
    } else {
        owner.declare_deferred(storage, false, declarations);
        owner.initialize_deferred(storage, raw(recipe), statements);
    }
    auto access = name_expression(outcome);
    if (!direct) {
        access = dereference_expression(std::move(access));
    }
    if (owner.context.semantic().may_stop_test(callee_type)) {
        auto stopped_access = name_expression(outcome);
        if (!direct) {
            stopped_access = dereference_expression(std::move(stopped_access));
        }
        auto stopped = template_call_expression(
            member_expression(
                std::move(stopped_access),
                TargetIdentifier::from_spelling("failure_if")
            ),
            {owner.context.intrinsic_type(TargetSymbol::RuntimeTestStopped)},
            {}
        );
        auto exit = LoweringStmtBuilder();
        owner.emit_test_exit(exit);
        statements.record_exits(exit.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = std::move(stopped), .body = std::move(exit).finish()});
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    auto success = call_member(std::move(access), "success_if", {});
    if (project_success) {
        const auto name = owner.names.fresh(TargetTemporaryNameKind::SuccessProjection);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = name,
                .type = owner.context.pointer_type(owner.context.intrinsic_type(
                    TargetSymbol::Auto,
                    use == ConstructionUse::ReadBorrow
                        || use == ConstructionUse::ConstPlace
                        || use == ConstructionUse::AddressValue
                        || use == ConstructionUse::OperandValue
                        || (use == ConstructionUse::Consume && scalar(source(recipe).type))
                )),
                .initializer = std::move(success)
            }
        ));
        success = name_expression(name);
        complete(recipe, Saved {.name = name, .kind = SavedKind::Success});
    } else {
        complete(recipe, LoweringCompleted {});
    }
    auto failure = owner.dispatch_failure(
        OutcomeFailureSource {.storage = outcome, .deferred = !direct},
        transport.failures,
        transport.destination
    );
    statements.record_exits(failure.exits());
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back(
        {.condition = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(success)),
         .body = std::move(failure).finish()}
    );
    statements.emit(generated_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
}

auto BodyRealizer::expression(
    ConstructionExpressionID source,
    RealizationLiteralContext literal,
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

auto BodyRealizer::operand(ConstructionOperand source, RealizationLiteralContext literal) noexcept
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
            expression(source, RealizationLiteralContext::Exact, ResultDemand::Discard)
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
    auto value = statements.accept(
        expression(source, RealizationLiteralContext::Exact, ResultDemand::Observe)
    );
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
