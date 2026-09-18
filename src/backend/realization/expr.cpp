module carven:backend.realization.expr.impl;

import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
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
import :support.task;
import :support.visit;
import std;

BodyRealizer::ExpressionBuilder::ExpressionBuilder(
    BodyRealizer& owner,
    const SemanticExpression& source
) noexcept
    : owner(owner),
      cleanup(owner.preparation.operation(source).lifetime),
      previous_frame(std::exchange(owner.active_frame, this)) {
    static_cast<void>(owner.metadata.lifetime_regions().region(cleanup));
}

BodyRealizer::ExpressionBuilder::~ExpressionBuilder() noexcept {
    owner.active_frame = previous_frame;
}

auto BodyRealizer::ExpressionBuilder::owns(const SemanticExpression& source) const noexcept
    -> bool {
    return owner.preparation.operation(source).lifetime == cleanup;
}

auto BodyRealizer::ExpressionBuilder::evaluate(
    const SemanticExpression& source,
    ConstantLiteralContext literal,
    ResultDemand demand,
    PreparedUse use
) noexcept -> ContinuationTask<Lowered<LoweringResult>> {
    auto outer = std::move(statements);
    statements = LoweringStmtBuilder();
    auto result = (co_await finish_expression(source, literal, demand, use, true));
    statements = std::move(outer);
    co_return result;
}

auto BodyRealizer::ExpressionBuilder::deliver_structured(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination,
    bool shared
) noexcept -> ContinuationTask<std::monostate> {
    auto outer = std::move(statements);
    statements = LoweringStmtBuilder();
    (co_await owner.structured_expression(source, result, statements));
    destination.append(take_statements(shared));
    statements = std::move(outer);
    co_return {};
}

auto BodyRealizer::ExpressionBuilder::finish_expression(
    const SemanticExpression& source,
    ConstantLiteralContext literal,
    ResultDemand demand,
    PreparedUse final_use,
    bool shared
) noexcept -> ContinuationTask<Lowered<LoweringResult>> {
    auto fragment = (co_await build(
        source,
        demand != ResultDemand::Discard,
        final_use,
        !shared,
        demand == ResultDemand::PropagateOutcome,
        literal,
        demand == ResultDemand::DirectReturn
    ));
    adopt(fragment);
    if (!statements.continues()) {
        co_return take_statements(shared).complete<LoweringResult>(std::nullopt);
    }
    if (std::holds_alternative<LoweringCompleted>(fragment.completion)) {
        co_return take_statements(shared).complete<LoweringResult>(LoweringCompleted {});
    }
    if (demand == ResultDemand::Discard) {
        discard_pending(fragment);
        adopt(fragment);
        co_return take_statements(shared).complete<LoweringResult>(LoweringCompleted {});
    }
    if (demand == ResultDemand::PropagateOutcome) {
        co_return take_statements(shared).complete<LoweringResult>(
            LoweringDirectExpression {std::get<TargetExpr>(std::move(fragment.completion))}
        );
    }
    auto result = emit(fragment, final_use, literal);
    co_return take_statements(shared).complete<LoweringResult>(
        LoweringDirectExpression {std::move(result)}
    );
}

auto BodyRealizer::ExpressionBuilder::initialize_expression(
    const SemInitialize& initialization,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    auto fragment = (co_await build(
        initialization.initializer,
        true,
        PreparedUse::Consume,
        true,
        false,
        ConstantLiteralContext::TargetTyped,
        false,
        false
    ));
    adopt(fragment);
    if (!statements.continues()) {
        destination.scope(take_statements());
        co_return {};
    }
    auto value = emit(fragment, PreparedUse::Consume, ConstantLiteralContext::TargetTyped);
    if (statements.empty() && declarations.empty()) {
        owner.declare_binding(initialization.binding, std::move(value), destination);
        co_return {};
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
        co_return {};
    }
    // Final storage belongs to the binding's lexical region. Pending source
    // owners belong to this full expression and die after direct delivery.
    const auto storage = LoweringDeferredStorage {
        .local = owner.binding_locals.at(initialization.binding),
        .value_type = owner.context.lower_type(owner.metadata.binding(initialization.binding).type)
    };
    owner.delayed_bindings.emplace(initialization.binding, storage);
    owner.declare_deferred(storage, true, destination);
    owner.initialize_deferred(storage, std::move(value), statements);
    destination.scope(take_statements());
    co_return {};
}

auto BodyRealizer::ExpressionBuilder::assign(
    const SemAssign& assignment,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    auto target = (co_await build(assignment.target, true, PreparedUse::WritePlace));
    adopt(target);
    if (!statements.continues()) {
        destination.scope(take_statements());
        co_return {};
    }
    if (!stable_place_binding(source(target).operation)
        && (assignment.compound
            || source(target).requires_execution
            || owner.preparation.summary(assignment.value).requires_execution)) {
        // Even an otherwise pure dereference selects a place before the RHS
        // may rebind its pointer slot. Do not use the value-read anchor gate.
        const auto name = owner.fresh_local(TargetTemporaryNameKind::Owner);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::RvalueReference,
                .maybe_unused = false,
                .local = name,
                .type = owner.context.intrinsic_type(TargetSymbol::Auto),
                .initializer = emit(target, PreparedUse::WritePlace)
            }
        ));
        complete(target, Saved {.local = name, .kind = SavedKind::Place});
    }
    const auto target_type = source(target).operation.type.resolved();
    const auto external =
        std::holds_alternative<CppTypeValue>(
            owner.context.semantic().types().type(target_type).value
        )
        || std::holds_alternative<CppTypeValue>(
            owner.context.semantic()
                .types()
                .type(owner.preparation.operation(assignment.value).type.resolved())
                .value
        );
    auto previous = std::optional<TargetLocalID>();
    if (assignment.compound
        && !external
        && owner.preparation.summary(assignment.value).requires_execution) {
        const auto name = owner.fresh_local(TargetTemporaryNameKind::Operand);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = name,
                .type = owner.context.lower_type(target_type),
                .initializer = raw(target)
            }
        ));
        previous = name;
    }
    auto right = (co_await build(assignment.value, true, PreparedUse::Consume, true));
    adopt(right);
    if (statements.continues()) {
        auto value = emit(right, PreparedUse::Consume, ConstantLiteralContext::Exact);
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
            TargetAssignmentStmt {
                .target = emit(target, PreparedUse::WritePlace),
                .op = operation,
                .value = std::move(value)
            }
        ));
    }
    destination.scope(take_statements());
    co_return {};
}

auto BodyRealizer::expression(
    const SemanticExpression& source,
    ConstantLiteralContext literal,
    ResultDemand demand
) noexcept -> ContinuationTask<Lowered<LoweringResult>> {
    if (active_frame != nullptr && active_frame->owns(source)) {
        co_return (co_await active_frame->evaluate(source, literal, demand, PreparedUse::Consume));
    }
    co_return co_await ExpressionBuilder(*this, source).finish_expression(source, literal, demand);
}

auto BodyRealizer::operand(PreparedOperand source, ConstantLiteralContext literal) noexcept
    -> ContinuationTask<Lowered<TargetExpr>> {
    auto result = co_await (
        active_frame != nullptr && active_frame->owns(*source.expression)
            ? active_frame->evaluate(*source.expression, literal, ResultDemand::Value, source.use)
            : ExpressionBuilder(*this, *source.expression)
                  .finish_expression(*source.expression, literal, ResultDemand::Value, source.use)
    );
    auto statements = LoweringStmtBuilder();
    auto value = statements.accept(std::move(result));
    co_return std::move(statements)
        .complete<TargetExpr>(
            value ? std::optional(require_expression(std::move(*value))) : std::nullopt
        );
}

auto BodyRealizer::discard(const SemanticExpression& source) noexcept
    -> ContinuationTask<Lowered<LoweringCompleted>> {
    auto statements = LoweringStmtBuilder();
    if (preparation.summary(source).requires_execution) {
        static_cast<void>(statements.accept(
            (co_await expression(source, ConstantLiteralContext::Exact, ResultDemand::Discard))
        ));
    }
    const auto normal = statements.continues() ? std::optional(LoweringCompleted {}) : std::nullopt;
    co_return std::move(statements).complete<LoweringCompleted>(normal);
}

auto BodyRealizer::condition(const SemanticExpression& source) noexcept
    -> ContinuationTask<Lowered<LoweringPredicate>> {
    const auto& source_value = preparation.operation(source);
    const auto shared = active_frame != nullptr && active_frame->owns(source);
    if (source_value.constant) {
        if (const auto* known = std::get_if<BooleanConstant>(
                &context.semantic().constants().constant(*source_value.constant).value
            )) {
            auto statements = LoweringStmtBuilder();
            const auto completion = statements.accept((co_await discard(source)));
            if (!shared) {
                auto completed = LoweringStmtBuilder();
                completed.scope(std::move(statements));
                statements = std::move(completed);
            }
            co_return std::move(statements)
                .complete<LoweringPredicate>(
                    completion ? std::optional<LoweringPredicate>(LoweringKnownBool {known->value})
                               : std::nullopt
                );
        }
    }
    auto statements = LoweringStmtBuilder();
    auto value = statements.accept((co_await operand(
        {.expression = std::addressof(source),
         .use = PreparedUse::OperandValue,
         .demand = PreparedDemand::Value},
        ConstantLiteralContext::Exact
    )));
    if (!value) {
        co_return std::move(statements).complete<LoweringPredicate>(std::nullopt);
    }
    auto expression_value = std::move(*value);
    if (shared || statements.empty()) {
        co_return std::move(statements)
            .complete<LoweringPredicate>(LoweringDynamicBool {std::move(expression_value)});
    }
    // A statement-form condition has its own source full expression. Deliver
    // its scalar before cleanup; branch execution starts after that scope ends.
    const auto name = fresh_local(TargetTemporaryNameKind::Operand);
    auto completed = LoweringStmtBuilder();
    completed.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .local = name,
            .type = context.lower_type(preparation.operation(source).type.resolved()),
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
    co_return std::move(completed).complete<LoweringPredicate>(
        LoweringDynamicBool {name_expression(name)}
    );
}

auto BodyRealizer::initialize_binding(
    const SemInitialize& source,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    const auto& initializer = preparation.operation(source.initializer);
    const auto escapes = source.initializer.exits_test
        || !context.semantic()
                .failure_sets()
                .failure_set(source.initializer.failures.resolved())
                .members.empty();
    if (escapes
        && (std::holds_alternative<SemIf>(initializer.value)
            || std::holds_alternative<SemMatch>(initializer.value)
            || std::holds_alternative<SemTry>(initializer.value))) {
        const auto storage = LoweringDeferredStorage {
            .local = binding_locals.at(source.binding),
            .value_type = context.lower_type(metadata.binding(source.binding).type)
        };
        delayed_bindings.emplace(source.binding, storage);
        declare_deferred(storage, true, destination);
        auto statements = LoweringStmtBuilder();
        (co_await result_expression(
            source.initializer,
            LoweringInitializeResult {.storage = storage},
            statements
        ));
        destination.scope(std::move(statements));
        co_return {};
    }
    (co_await ExpressionBuilder(*this, source.initializer)
         .initialize_expression(source, destination));
    co_return {};
}

auto BodyRealizer::assign(const SemAssign& source, LoweringStmtBuilder& destination) noexcept
    -> ContinuationTask<std::monostate> {
    (co_await ExpressionBuilder(*this, source.value).assign(source, destination));
    co_return {};
}

auto BodyRealizer::structured_delivery(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    if (active_frame != nullptr && active_frame->owns(source)) {
        (co_await active_frame->deliver_structured(source, result, destination, true));
        co_return {};
    }
    (co_await ExpressionBuilder(*this, source)
         .deliver_structured(source, result, destination, false));
    co_return {};
}
