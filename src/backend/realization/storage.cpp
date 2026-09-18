module carven:backend.realization.storage.impl;

import :backend.preparation.body;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.report;
import :backend.realization.expr;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.body;
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

auto BodyRealizer::ExpressionBuilder::take_statements(bool shared) noexcept -> LoweringStmtBuilder {
    if (shared) {
        return std::move(statements);
    }
    declarations.append(std::move(statements));
    return std::move(declarations);
}

auto BodyRealizer::ExpressionBuilder::borrowed_owner(
    const PreparedOperation& value,
    PreparedUse use
) const noexcept -> bool {
    return value.operation.category == SemanticValueCategory::Value
        && !scalar(value.operation.type.resolved())
        && !std::holds_alternative<SliceTypeValue>(
               owner.context.semantic().types().type(value.operation.type.resolved()).value
        )
        && use != PreparedUse::Consume
        && use != PreparedUse::NativeTake;
}

auto BodyRealizer::ExpressionBuilder::preserve_borrows(Recipe& recipe) noexcept
    -> ContinuationTask<std::monostate> {
    if (!std::holds_alternative<std::monostate>(recipe.completion)
        || std::holds_alternative<SemBinding>(source(recipe).operation.value)
        || (source(recipe).operation.constant
            && !source(recipe).requires_execution
            && scalar(source(recipe).operation.type.resolved()))) {
        co_return {};
    }
    const auto& inputs = recipe.inputs;
    for (auto index = 0uz; index < inputs.size(); ++index) {
        auto& child = *recipe.operands[index];
        const auto& value = source(child);
        // Named storage already has its source lifetime. Sequencing owns any
        // snapshot needed before a later operand executes.
        if (std::holds_alternative<SemCallable>(value.operation.value)
            || std::holds_alternative<SemBinding>(value.operation.value)) {
            continue;
        }
        if (!scalar(value.operation.type.resolved())
            && value.operation.category == SemanticValueCategory::Value
            && inputs[index].use != PreparedUse::Consume
            && inputs[index].use != PreparedUse::NativeTake) {
            (co_await anchor(child, inputs[index].use, true));
        } else if (!saved(child) && !std::holds_alternative<SemBinding>(value.operation.value)) {
            (co_await preserve_borrows(child));
        }
    }
    co_return {};
}

auto BodyRealizer::ExpressionBuilder::raw(Recipe& recipe, ConstantLiteralContext literal) noexcept
    -> ContinuationTask<TargetExpr> {
    const auto& value = source(recipe);
    const auto observed =
        owner.test_observation && owner.test_observation->expression == recipe.expression;
    if (!observed
        && value.operation.constant
        && !value.requires_execution
        && scalar(value.operation.type.resolved())) {
        co_return constant_expression(owner.context, *value.operation.constant, literal);
    }
    if (std::holds_alternative<LoweringCompleted>(recipe.completion)) {
        invariant_violation("completed operation has no residual value");
    }
    if (auto* residual = std::get_if<TargetExpr>(&recipe.completion)) {
        co_return std::move(*residual);
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
        co_return result;
    }
    if (const auto* binding = std::get_if<SemBinding>(&value.operation.value)) {
        co_return owner.binding_expression(binding->binding);
    }
    if (const auto* constant = std::get_if<SemConstant>(&value.operation.value)) {
        co_return constant_expression(owner.context, constant->constant, literal);
    }
    if (std::holds_alternative<SemTake>(value.operation.value)) {
        co_return transfer_expression(
            (co_await emit(*recipe.operands.front(), PreparedUse::WritePlace))
        );
    }
    if (const auto* adoption = std::get_if<SemArrayAdopt>(&value.operation.value)) {
        co_return realize_callable_adaptation(
            owner.context,
            (co_await raw(*recipe.operands.front())),
            adoption->source->type.resolved(),
            value.operation.type.resolved()
        );
    }

    // These checked helpers take both operands as their explicit <T>.
    const auto* binary = std::get_if<SemBinary>(&value.operation.value);
    const auto typed_arithmetic = binary != nullptr
        && owner.context.is_integer(value.operation.type.resolved())
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
    const auto append_operand =
        [&](std::size_t index) noexcept -> ContinuationTask<std::monostate> {
        operands.push_back((co_await emit(
            *recipe.operands[index],
            inputs[index].use,
            inputs[index].use == PreparedUse::OperandValue
                ? (typed_arithmetic ? ConstantLiteralContext::TargetTyped : literal)
                : std::holds_alternative<SemArray>(value.operation.value)
                ? ConstantLiteralContext::TargetTyped
                : ConstantLiteralContext::Exact
        )));
        co_return {};
    };
    for (auto index = 0uz; index < inputs.size(); ++index) {
        if (inputs[index].demand == PreparedDemand::Value) {
            (co_await append_operand(index));
        }
    }
    if (observed && binary != nullptr) {
        co_return realize_observed_comparison(
            owner.context,
            *binary,
            std::move(operands),
            owner.test_observation->writer,
            owner.test_observation->sources
        );
    }
    co_return realize_operation(
        owner.context,
        value.operation,
        value.preparation.get(),
        std::move(operands)
    );
}

auto BodyRealizer::ExpressionBuilder::emit(
    Recipe& recipe,
    PreparedUse use,
    ConstantLiteralContext literal
) noexcept -> ContinuationTask<TargetExpr> {
    if (use == PreparedUse::ProjectionPlace) {
        invariant_violation("projection access was not resolved before realization");
    }
    if (!saved(recipe) && (use == PreparedUse::WritePlace || use == PreparedUse::NativeTake)) {
        if (const auto* binding = std::get_if<SemBinding>(&source(recipe).operation.value);
            binding != nullptr
            && std::holds_alternative<OwnerBindingStorage>(
                owner.metadata.binding(binding->binding).storage
            )) {
            owner.mutable_owners.emplace(owner.binding_names.at(binding->binding).spelling());
        }
    }
    if (use == PreparedUse::NativeTake) {
        // The query promises T&&. Do not first turn a trivial Take into
        // const T& via Carven transfer and then cast away constness.
        auto value = saved(recipe) == nullptr
                && std::holds_alternative<SemTake>(source(recipe).operation.value)
            ? (co_await emit(*recipe.operands.front(), PreparedUse::WritePlace))
            : (co_await raw(recipe, literal));
        co_return TargetExpr {
            .value = TargetStaticCastExpr {
                .type = owner.context.reference_type(
                    owner.context.lower_type(source(recipe).operation.type.resolved()),
                    false,
                    true
                ),
                .operand = target_child(std::move(value))
            }
        };
    }
    auto result = (co_await raw(recipe, literal));
    if (use == PreparedUse::Consume
        && saved(recipe)
        && saved(recipe)->kind != SavedKind::Place
        && saved(recipe)->kind != SavedKind::StoredPlace) {
        if (saved(recipe)->kind == SavedKind::Success
            && scalar(source(recipe).operation.type.resolved())) {
            // Scalar transfer observes const T&; the success projection
            // already promises const access and cannot call transfer(T&).
            co_return TargetExpr {
                .value = TargetStaticCastExpr {
                    .type = owner.context.reference_type(
                        owner.context.lower_type(source(recipe).operation.type.resolved()),
                        true
                    ),
                    .operand = target_child(std::move(result))
                }
            };
        }
        co_return transfer_expression(std::move(result));
    }
    const auto& value = source(recipe);
    const auto copy_binding = (use == PreparedUse::Consume || use == PreparedUse::OperandValue)
        && !saved(recipe)
        && std::holds_alternative<SemBinding>(value.operation.value)
        && !scalar(value.operation.type.resolved());
    // Named values copy even at C++ automatic-move return sites.
    if (copy_binding) {
        co_return call_expression(
            intrinsic_expression(TargetSymbol::StdAsConst),
            target_expressions(std::move(result))
        );
    }
    if (use == PreparedUse::ReadBorrow
        || use == PreparedUse::ConstPlace
        || use == PreparedUse::AddressValue) {
        const auto type = owner.context.lower_type(source(recipe).operation.type.resolved());
        if (use == PreparedUse::AddressValue && !saved(recipe)) {
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
    co_return result;
}

auto BodyRealizer::ExpressionBuilder::anchor(
    Recipe& recipe,
    PreparedUse use,
    bool force,
    bool direct_scalar
) noexcept -> ContinuationTask<std::monostate> {
    if (!pending(recipe)) {
        co_return {};
    }
    const auto& value = source(recipe);
    if (!force && !value.reads_storage && !value.requires_execution) {
        co_return {};
    }
    if (value.operation.category == SemanticValueCategory::Value
        && !scalar(value.operation.type.resolved())
        && !std::holds_alternative<SemBinding>(value.operation.value)
        && value.operation.lifetime != cleanup) {
        invariant_violation("owner anchoring requires its source cleanup frame");
    }
    if (std::holds_alternative<SemBinding>(value.operation.value)
        && (use == PreparedUse::WritePlace || use == PreparedUse::ConstPlace)) {
        co_return {};
    }
    if (!std::holds_alternative<SemBinding>(value.operation.value)) {
        (co_await preserve_borrows(recipe));
    }
    const auto name = owner.names.fresh(TargetTemporaryNameKind::Owner);
    if ((direct_scalar || use == PreparedUse::OperandValue || use == PreparedUse::Consume)
        && scalar(value.operation.type.resolved())
        && use != PreparedUse::WritePlace
        && use != PreparedUse::ConstPlace
        && std::holds_alternative<BuiltinTypeValue>(
            owner.context.semantic().types().type(value.operation.type.resolved()).value
        )) {
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = use == PreparedUse::Consume || use == PreparedUse::NativeTake
                    ? TargetVariableBinding::MutableValue
                    : TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = name,
                .type = owner.context.lower_type(value.operation.type.resolved()),
                .initializer = (co_await raw(recipe))
            }
        ));
        complete(recipe, Saved {.name = name, .kind = SavedKind::Value});
        co_return {};
    }
    if (std::holds_alternative<SemCppCall>(value.operation.value)
        && use != PreparedUse::Consume
        && use != PreparedUse::NativeTake) {
        const auto* native = std::get_if<CppTypeValue>(
            &owner.context.semantic().types().type(value.operation.type.resolved()).value
        );
        const auto* query = native == nullptr ? nullptr : std::get_if<CppQueryType>(&native->form);
        if (query == nullptr) {
            invariant_violation("native call has no result query");
        }
        const auto exact = owner.context.lower_cpp_query(*query);
        const auto storage = LoweringDeferredStorage {.name = name, .value_type = exact};
        owner.declare_deferred(storage, false, declarations);
        owner.initialize_deferred(
            storage,
            (co_await raw(recipe)),
            statements,
            owner.context.intrinsic_type(TargetSymbol::DecltypeAuto)
        );
        complete(recipe, Saved {.name = name, .kind = SavedKind::StoredValue});
        co_return {};
    }
    // Consume completes a value snapshot at this barrier, not merely a
    // native invocation. Returning the normalized object type copies a
    // native T&/const T&, moves T&&, and directly constructs a prvalue.
    const auto place =
        (value.operation.category == SemanticValueCategory::Place || names_storage(value))
        && (use == PreparedUse::WritePlace || use == PreparedUse::ConstPlace);
    // Read describes the consumer, not ownership of a newly produced value.
    // A factory cannot extend a prvalue lifetime by returning const T&.
    const auto read = use == PreparedUse::ReadBorrow
        && (value.operation.category == SemanticValueCategory::Place || names_storage(value));
    // By-value builtin snapshots store the value type, not a const parameter type.
    const auto read_value =
        read
        && std::holds_alternative<BuiltinTypeValue>(
            owner.context.semantic().types().type(value.operation.type.resolved()).value
        )
        && !owner.context.plan().read_borrows_storage(value.operation.type.resolved());
    const auto type = place ? owner.context.reference_type(
                                  owner.context.lower_type(value.operation.type.resolved()),
                                  use == PreparedUse::ConstPlace
                                      || value.operation.category != SemanticValueCategory::Place
                              )
        : read && !read_value
        ? owner.context.lower_parameter(
              {.access = AccessMode::Read, .type = value.operation.type.resolved()}
          )
        : owner.context.lower_type(value.operation.type.resolved());
    const auto storage = LoweringDeferredStorage {.name = name, .value_type = type};
    owner.declare_deferred(storage, false, declarations);
    owner.initialize_deferred(
        storage,
        use == PreparedUse::Consume
                || use == PreparedUse::OperandValue
                || use == PreparedUse::WritePlace
            ? (co_await emit(recipe, use))
            : (co_await raw(recipe)),
        statements
    );
    complete(
        recipe,
        Saved {.name = name, .kind = place ? SavedKind::StoredPlace : SavedKind::StoredValue}
    );
    co_return {};
}
