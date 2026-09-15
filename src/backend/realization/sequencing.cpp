module carven:backend.realization.sequencing.impl;

import :backend.construction;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.expr;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir;
import std;

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
        if (owner.context.is_void(source(recipe).type)) {
            statements.emit(generated_statement(TargetExprStmt {.expression = raw(recipe)}));
        } else {
            statements.emit(generated_statement(TargetDiscardStmt {.expression = raw(recipe)}));
        }
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
