module carven:backend.realization.fragment.impl;

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

auto BodyRealizer::ExpressionBuilder::saved(const Fragment& fragment) const noexcept
    -> const Saved* {
    return std::get_if<Saved>(&fragment.completion);
}

auto BodyRealizer::ExpressionBuilder::source(const Fragment& fragment) const noexcept
    -> const PreparedOperation& {
    return fragment.preparation;
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

auto BodyRealizer::ExpressionBuilder::pending(const Fragment& fragment) const noexcept -> bool {
    return (std::holds_alternative<LocalBindingID>(fragment.completion)
            || std::holds_alternative<ConstantID>(fragment.completion))
        || std::holds_alternative<TargetExpr>(fragment.completion);
}

BodyRealizer::ExpressionBuilder::BuildScope::BuildScope(ExpressionBuilder& source) noexcept
    : frame(source),
      declarations(std::move(source.declarations)),
      statements(std::move(source.statements)) {
    frame.declarations = LoweringStmtBuilder();
    frame.statements = LoweringStmtBuilder();
}

BodyRealizer::ExpressionBuilder::BuildScope::~BuildScope() noexcept {
    frame.declarations = std::move(declarations);
    frame.statements = std::move(statements);
}

auto BodyRealizer::ExpressionBuilder::finish_fragment(Fragment value) noexcept -> Fragment {
    value.declarations.append(std::move(declarations));
    value.statements.append(std::move(statements));
    return value;
}

auto BodyRealizer::ExpressionBuilder::adopt(Fragment& value) noexcept -> void {
    declarations.append(std::move(value.declarations));
    statements.append(std::move(value.statements));
    value.declarations = LoweringStmtBuilder();
    value.statements = LoweringStmtBuilder();
}

auto BodyRealizer::ExpressionBuilder::stable_place_binding(
    const SemanticExpression& value
) const noexcept -> bool {
    const auto* binding = std::get_if<SemBinding>(&value.value);
    if (binding == nullptr) {
        return false;
    }
    const auto* capture =
        std::get_if<CaptureBindingStorage>(&owner.metadata.binding(binding->binding).storage);
    return capture == nullptr || capture->mode != CaptureMode::Write;
}

auto BodyRealizer::ExpressionBuilder::build(
    const SemanticExpression& expression,
    bool result_needed,
    PreparedUse result_use,
    bool propagate_outcome,
    ConstantLiteralContext literal,
    bool direct_return,
    bool retain_backing,
    std::size_t expression_depth
) noexcept -> ContinuationTask<Fragment> {
    const auto scope = BuildScope(*this);
    auto prepared_source = owner.preparation.prepare(expression);
    const auto& initial = prepared_source;
    if (!result_needed && initial.requires_execution && borrowed_owner(initial, result_use)) {
        result_needed = true;
    }
    auto fragment = Fragment {
        .preparation = std::move(prepared_source),
        .completion = LoweringCompleted {},
        .declarations = {},
        .statements = {},
        .executes = false,
        .observes = false
    };
    const auto& value = fragment.preparation;
    const auto* builtin_result = std::get_if<BuiltinTypeValue>(
        &owner.context.semantic().types().type(value.operation.type.resolved()).value
    );
    const auto bounded_scalar = builtin_result != nullptr
        && (owner.context.is_integer(value.operation.type.resolved())
            || builtin_result->kind == BuiltinType::Bool
            || builtin_result->kind == BuiltinType::Char);
    const auto depth_boundary = expression_depth >= 64uz
        && bounded_scalar
        && value.operation.category == SemanticValueCategory::Value
        && (std::holds_alternative<SemBinary>(value.operation.value)
            || std::holds_alternative<SemUnary>(value.operation.value));
    retain_backing |= depth_boundary;
    const auto observed = owner.condition_observation
        && owner.condition_observation->expression == std::addressof(expression);
    if (!observed
        && value.operation.constant
        && !value.requires_execution
        && scalar(value.operation.type.resolved())) {
        complete(fragment, *value.operation.constant);
        co_return finish_fragment(std::move(fragment));
    }
    if (!observed
        && result_needed
        && value.operation.constant
        && !value.executes_operation
        && !names_storage(value)
        && scalar(value.operation.type.resolved())) {
        auto effects = co_await build(expression, false, PreparedUse::OperandValue);
        discard_pending(effects);
        adopt(effects);
        if (statements.continues()) {
            complete(
                fragment,
                constant_expression(owner.context, *value.operation.constant, literal)
            );
        }
        co_return finish_fragment(std::move(fragment));
    }
    if (const auto* binding = std::get_if<SemBinding>(&value.operation.value)) {
        fragment.observes = value.reads_storage;
        complete(fragment, binding->binding);
        co_return finish_fragment(std::move(fragment));
    }
    if (const auto* constant = std::get_if<SemConstant>(&value.operation.value)) {
        complete(fragment, constant->constant);
        co_return finish_fragment(std::move(fragment));
    }
    if (const auto* report = std::get_if<SemReport>(&value.operation.value)) {
        (co_await owner.lower_report(*report, value.operation.origin, statements));
        co_return finish_fragment(std::move(fragment));
    }
    if (std::holds_alternative<SemIf>(value.operation.value)
        || std::holds_alternative<SemMatch>(value.operation.value)
        || std::holds_alternative<SemTry>(value.operation.value)) {
        if (!result_needed) {
            (co_await owner
                 .structured_expression(expression, LoweringDiscardResult {}, statements));
        } else if (!value.operation.exits_test
                   && owner.context.semantic()
                          .failure_sets()
                          .failure_set(value.operation.failures.resolved())
                          .members.empty()) {
            const auto yield = owner.exit_target(LoweringExitKind::Value);
            auto body = LoweringStmtBuilder();
            (co_await owner
                 .structured_expression(expression, LoweringYieldResult {.target = yield}, body));
            fragment.executes = value.requires_execution;
            fragment.observes = value.reads_storage;
            complete(
                fragment,
                std::move(body)
                    .result_region(owner.context.lower_type(value.operation.type.resolved()), yield)
            );
        } else {
            const auto storage = LoweringDeferredStorage {
                .local = owner.fresh_local(TargetTemporaryNameKind::Owner),
                .value_type = owner.context.lower_type(value.operation.type.resolved())
            };
            auto outer = std::move(declarations);
            declarations = LoweringStmtBuilder();
            (co_await owner.structured_expression(
                expression,
                LoweringInitializeResult {.storage = storage},
                statements
            ));
            if (statements.continues()) {
                owner.declare_deferred(storage, true, outer);
                complete(fragment, Saved {.local = storage.local, .kind = SavedKind::StoredValue});
            }
            outer.append(std::move(declarations));
            declarations = std::move(outer);
        }
        co_return finish_fragment(std::move(fragment));
    }
    if (const auto* logic = std::get_if<SemShortCircuit>(&value.operation.value)) {
        if (!result_needed && !owner.preparation.summary(*logic->right).requires_execution) {
            auto child = co_await build(*logic->left, false, PreparedUse::OperandValue);
            adopt(child);
            fragment.executes = has_effect(child);
            fragment.observes = has_storage_read(child);
            complete(fragment, std::move(child.completion));
            co_return finish_fragment(std::move(fragment));
        }
        const auto observe = [&](bool left, std::optional<TargetExpr> right) noexcept {
            return realize_observed_short_circuit(
                owner.context,
                owner.condition_observation->writer,
                owner.condition_observation->sources,
                left,
                std::move(right)
            );
        };
        const auto constant = owner.preparation.operation(*logic->left).constant;
        const auto* known = constant
            ? std::get_if<BooleanConstant>(
                  &owner.context.semantic().constants().constant(*constant).value
              )
            : nullptr;
        auto condition = co_await build(*logic->left, known == nullptr, PreparedUse::OperandValue);
        if (known) {
            discard_pending(condition);
        }
        adopt(condition);
        if (!statements.continues()) {
            co_return finish_fragment(std::move(fragment));
        }
        if (known != nullptr) {
            if (known->value == (logic->operation == ShortCircuitOperator::And)) {
                auto selected =
                    co_await build(*logic->right, result_needed, PreparedUse::OperandValue);
                adopt(selected);
                if (observed && statements.continues()) {
                    complete(
                        fragment,
                        observe(known->value, emit(selected, PreparedUse::OperandValue))
                    );
                } else {
                    fragment.executes = has_effect(selected);
                    fragment.observes = has_storage_read(selected);
                    complete(fragment, std::move(selected.completion));
                }
            } else if (result_needed) {
                complete(
                    fragment,
                    observed && !known->value ? observe(false, std::nullopt)
                                              : bool_expression(known->value)
                );
            }
            co_return finish_fragment(std::move(fragment));
        }
        fragment.executes = has_effect(condition);
        fragment.observes = has_storage_read(condition);
        auto test = emit(condition, PreparedUse::OperandValue);
        auto selected = co_await build(*logic->right, result_needed, PreparedUse::OperandValue);
        fragment.executes |= has_effect(selected);
        fragment.observes |= has_storage_read(selected);
        declarations.append(std::move(selected.declarations));
        auto selected_value = std::optional<TargetExpr>();
        if (selected.statements.continues()) {
            if (result_needed) {
                selected_value = emit(selected, PreparedUse::OperandValue);
                if (observed) {
                    selected_value = observe(
                        logic->operation == ShortCircuitOperator::And,
                        std::move(selected_value)
                    );
                }
            } else {
                discard_pending(selected);
            }
        }
        if (selected_value
            && selected.statements.empty()
            && !(observed && logic->operation == ShortCircuitOperator::And)) {
            complete(
                fragment,
                binary_expression(
                    std::move(test),
                    logic->operation == ShortCircuitOperator::And ? TargetBinaryOperator::LogicalAnd
                                                                  : TargetBinaryOperator::LogicalOr,
                    std::move(*selected_value)
                )
            );
            co_return finish_fragment(std::move(fragment));
        }
        auto name = std::optional<TargetLocalID>();
        if (result_needed) {
            name = owner.fresh_local(TargetTemporaryNameKind::Operand);
            declarations.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .local = *name,
                    .type = owner.context.lower_type(value.operation.type.resolved()),
                    .initializer = bool_expression(logic->operation == ShortCircuitOperator::Or)
                }
            ));
            if (selected_value) {
                selected.statements.emit(generated_statement(
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
        statements.record_exits(selected.statements.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = std::move(test), .body = std::move(selected.statements).finish()}
        );
        auto skipped = std::optional<std::vector<TargetStmt>>();
        if (observed && logic->operation == ShortCircuitOperator::And) {
            skipped.emplace();
            skipped->push_back(statement_expression(observe(false, std::nullopt)));
        }
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::move(skipped)}
        ));
        if (name) {
            complete(fragment, Saved {.local = *name, .kind = SavedKind::Value});
        }
        co_return finish_fragment(std::move(fragment));
    }
    auto inputs = value.operands;
    for (auto& input : inputs) {
        if (input.use == PreparedUse::ProjectionPlace) {
            input.use =
                result_use == PreparedUse::WritePlace || result_use == PreparedUse::NativeTake
                ? PreparedUse::WritePlace
                : PreparedUse::ConstPlace;
        }
    }
    auto children = std::vector<Fragment>();
    children.reserve(inputs.size());
    const auto suffix_begin = sequenced_suffix_begin(value);
    const auto postfix_end = first_unsequenced(value);
    const auto direct_scalars = independent_scope;
    auto effects = std::vector<std::size_t>();
    auto reads = std::vector<std::size_t>();
    auto effect_cursor = 0uz;
    auto read_cursor = 0uz;
    auto postfix_cursor = 0uz;
    const auto commit = [&](std::size_t index) noexcept -> void {
        anchor(children[index], inputs[index].use, false, direct_scalars);
        adopt(children[index]);
    };
    const auto commit_postfix = [&]() noexcept -> void {
        while (postfix_cursor < std::min(postfix_end, children.size())) {
            commit(postfix_cursor++);
        }
    };
    const auto commit_prior = [&](bool include_reads, std::size_t before) noexcept -> void {
        auto committed = false;
        while (effect_cursor < effects.size() || (include_reads && read_cursor < reads.size())) {
            const auto effect = effect_cursor < effects.size()
                ? effects[effect_cursor]
                : std::numeric_limits<std::size_t>::max();
            const auto read = include_reads && read_cursor < reads.size()
                ? reads[read_cursor]
                : std::numeric_limits<std::size_t>::max();
            const auto index = std::min(effect, read);
            if (index >= before) {
                break;
            }
            if (effect < read) {
                ++effect_cursor;
            } else {
                ++read_cursor;
            }
            if (!has_effect(children[index]) && !has_storage_read(children[index])) {
                continue;
            }
            if (!committed) {
                commit_postfix();
                committed = true;
            }
            commit(index);
        }
    };
    const auto* binary = std::get_if<SemBinary>(&value.operation.value);
    const auto* arithmetic = std::get_if<PreparedBinary>(value.preparation.get());
    const auto typed_arithmetic = arithmetic != nullptr && arithmetic->target_typed_operands;
    // Construct later fragments first so the storage demand of an earlier
    // source occurrence is known before its residual tree is constructed.
    // Only the following forward pass adopts statements and declarations.
    auto constructed = std::vector<std::optional<Fragment>>(inputs.size());
    auto later_prefix = false;
    auto later_effect = false;
    auto later_read = false;
    const auto* format_preparation = std::get_if<PreparedFormat>(value.preparation.get());
    const auto multiple_uses = format_preparation != nullptr
        && std::holds_alternative<PreparedWriterFormat>(*format_preparation);
    for (auto index = inputs.size(); index != 0uz;) {
        --index;
        const auto& input = inputs[index];
        const auto& input_source = owner.preparation.summary(*input.expression);
        const auto crosses = later_prefix
            || (index < postfix_end && later_effect)
            || (index < suffix_begin
                && ((input_source.requires_execution && (later_effect || later_read))
                    || (input_source.reads_storage && later_effect)));
        const auto keep = input.demand == PreparedDemand::Value;
        const auto child_literal = input.use == PreparedUse::OperandValue
            ? (typed_arithmetic ? ConstantLiteralContext::TargetTyped
                                : ConstantLiteralContext::Exact)
            : std::holds_alternative<SemArray>(value.operation.value)
            ? ConstantLiteralContext::TargetTyped
            : ConstantLiteralContext::Exact;
        auto child = co_await build(
            *input.expression,
            keep && (result_needed || value.executes_operation),
            input.use,
            false,
            child_literal,
            false,
            retain_backing || crosses || multiple_uses,
            depth_boundary ? 0uz : expression_depth + 1uz
        );
        if (retain_backing || crosses || multiple_uses) {
            retain_input(child, input.use);
        }
        if (!keep || (!result_needed && !value.executes_operation)) {
            discard_pending(child);
        }
        later_prefix |= !child.statements.empty();
        later_effect |= has_effect(child);
        later_read |= has_storage_read(child);
        constructed[index].emplace(std::move(child));
    }
    for (auto index = 0uz; index < inputs.size(); ++index) {
        auto child = std::move(*constructed[index]);
        if (!child.statements.empty()) {
            commit_postfix();
            commit_prior(true, std::numeric_limits<std::size_t>::max());
        } else if (suffix_begin != 0uz && (has_effect(child) || has_storage_read(child))) {
            if (has_effect(child)) {
                commit_postfix();
            }
            commit_prior(has_effect(child), suffix_begin);
        }
        adopt(child);
        if (!statements.continues()) {
            co_return finish_fragment(std::move(fragment));
        }
        if (index >= postfix_end) {
            if (has_effect(child)) {
                effects.push_back(index);
            } else if (has_storage_read(child)) {
                reads.push_back(index);
            }
        }
        children.push_back(std::move(child));
    }
    const auto* format = std::get_if<SemFormat>(&value.operation.value);
    const auto* prepared = std::get_if<PreparedFormat>(value.preparation.get());
    const auto* writer =
        prepared != nullptr ? std::get_if<PreparedWriterFormat>(prepared) : nullptr;
    if (format != nullptr && writer != nullptr && (format->receiver || direct_return)) {
        complete_writer(
            fragment,
            *format,
            *writer,
            children,
            inputs,
            direct_return ? std::optional(owner.fresh_local(TargetTemporaryNameKind::Owner))
                          : std::nullopt
        );
        co_return finish_fragment(std::move(fragment));
    }
    if (!result_needed && !value.executes_operation) {
        co_return finish_fragment(std::move(fragment));
    }
    const auto* field = std::get_if<SemField>(&value.operation.value);
    const auto owning_field = field != nullptr && field->consumes_source();
    const auto* adaptation = std::get_if<PreparedCallableAdaptation>(value.preparation.get());
    const auto needs_source = owning_field
        || (adaptation != nullptr
            && (adaptation->array || adaptation->adaptation.borrows_storage()));
    if (needs_source) {
        anchor(children.front(), inputs.front().use, true);
        adopt(children.front());
    }
    fragment.executes = value.executes_operation;
    fragment.observes = value.executes_operation;
    for (const auto& child : children) {
        fragment.executes |= has_effect(child);
        fragment.observes |= has_storage_read(child);
    }
    auto operands = std::vector<TargetExpr>();
    for (auto index = 0uz; index < inputs.size(); ++index) {
        if (inputs[index].demand != PreparedDemand::Value) {
            continue;
        }
        const auto child_literal = inputs[index].use == PreparedUse::OperandValue
            ? (typed_arithmetic ? ConstantLiteralContext::TargetTyped
                                : ConstantLiteralContext::Exact)
            : std::holds_alternative<SemArray>(value.operation.value)
            ? ConstantLiteralContext::TargetTyped
            : ConstantLiteralContext::Exact;
        // An owning projection keeps the complete source alive for cleanup,
        // then transfers only the selected field from its mutable storage.
        operands.push_back(emit(
            children[index],
            owning_field ? PreparedUse::WritePlace : inputs[index].use,
            child_literal
        ));
    }
    if (std::holds_alternative<SemTake>(value.operation.value)) {
        complete(
            fragment,
            result_use == PreparedUse::NativeTake ? std::move(operands.front())
                                                  : transfer_expression(std::move(operands.front()))
        );
    } else if (owning_field) {
        auto projected = realize_operation(
            owner.context,
            value.operation,
            value.preparation.get(),
            std::move(operands)
        );
        complete(
            fragment,
            result_use == PreparedUse::NativeTake ? std::move(projected)
                                                  : transfer_expression(std::move(projected))
        );
    } else if (adaptation != nullptr && adaptation->array) {
        complete(
            fragment,
            realize_callable_adaptation(
                owner.context,
                std::move(operands.front()),
                *adaptation,
                value.operation.type.resolved()
            )
        );
    } else if (observed && binary != nullptr) {
        complete(
            fragment,
            realize_observed_comparison(
                owner.context,
                *binary,
                std::move(operands),
                owner.condition_observation->writer,
                owner.condition_observation->sources
            )
        );
    } else {
        complete(
            fragment,
            realize_operation(
                owner.context,
                value.operation,
                value.preparation.get(),
                std::move(operands)
            )
        );
    }
    if (depth_boundary) {
        anchor(fragment, result_use, true, true);
        adopt(fragment);
    }
    if (const auto transport = owner.fallible(value.operation)) {
        complete_call(
            fragment,
            *transport,
            result_needed && !owner.context.is_void(value.operation.type.resolved()),
            result_use,
            propagate_outcome
        );
    }
    if (!result_needed) {
        discard_pending(fragment);
        adopt(fragment);
    }
    co_return finish_fragment(std::move(fragment));
}
