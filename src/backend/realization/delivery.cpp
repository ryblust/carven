module carven:backend.realization.delivery.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.composition;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import std;

auto BodyRealizer::read_value(
    Lowered<LoweringResult> evaluation,
    LoweringStmtBuilder& destination
) noexcept -> std::optional<TargetExpr> {
    auto result = destination.accept(std::move(evaluation));
    if (!result) {
        return std::nullopt;
    }
    return require_expression(std::move(*result));
}

auto BodyRealizer::initialize_deferred(
    const LoweringDeferredStorage& storage,
    TargetExpr initializer,
    LoweringStmtBuilder& destination,
    std::optional<TargetTypeID> factory_result
) noexcept -> void {
    // Return the complete initializer to preserve copy-initialization semantics.
    // Explicit construction expressions remain explicit inside this factory.
    const auto yield = exit_target(LoweringExitKind::Value);
    auto value = LoweringStmtBuilder();
    emit_return(std::move(initializer), value, LoweringYieldResult {.target = yield});
    auto factory =
        std::move(value).result_factory(factory_result.value_or(storage.value_type), yield);
    destination.emit(statement_expression(call_member(
        name_expression(storage.name),
        "initialize",
        target_expressions(std::move(factory))
    )));
}

auto BodyRealizer::deliver_result(
    LoweringResult value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (returns_result(result)) {
        emit_return(remaining_expression(std::move(value)), destination, result);
    } else if (const auto* initialize = std::get_if<LoweringInitializeResult>(&result)) {
        initialize_deferred(initialize->storage, require_expression(std::move(value)), destination);
    } else if (auto expression = remaining_expression(std::move(value))) {
        destination.emit(
            generated_statement(TargetDiscardStmt {.expression = std::move(*expression)})
        );
    }
}
