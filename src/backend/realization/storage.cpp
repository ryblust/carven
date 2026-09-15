module carven:backend.realization.storage.impl;

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

auto BodyRealizer::ExpressionBuilder::take_statements(bool shared) noexcept -> LoweringStmtBuilder {
    if (shared) {
        return std::move(statements);
    }
    declarations.append(std::move(statements));
    return std::move(declarations);
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
        // Named storage already has its source lifetime. Sequencing owns any
        // snapshot needed before a later operand executes.
        if (std::holds_alternative<SemCallable>(value.operation.value)
            || std::holds_alternative<SemBinding>(value.operation.value)) {
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

auto BodyRealizer::ExpressionBuilder::raw(Recipe& recipe, ConstantLiteralContext literal) noexcept
    -> TargetExpr {
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
        return realize_callable_adaptation(
            owner.context,
            raw(recipe.operands.front()),
            adoption->source->type.resolved(),
            value.type
        );
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
    const auto append_operand = [&](std::size_t index) noexcept {
        operands.push_back(emit(
            recipe.operands[index],
            inputs[index].use,
            inputs[index].use == ConstructionUse::OperandValue
                ? (typed_arithmetic ? ConstantLiteralContext::TargetTyped : literal)
                : std::holds_alternative<SemArray>(value.operation.value)
                ? ConstantLiteralContext::TargetTyped
                : ConstantLiteralContext::Exact
        ));
    };
    for (auto index = 0uz; index < inputs.size(); ++index) {
        if (inputs[index].demand == ConstructionDemand::Value) {
            append_operand(index);
        }
    }
    const auto* operation = std::get_if<ConstructionOperation>(&value.value);
    if (operation == nullptr) {
        invariant_violation("ordinary realization requires an operation");
    }
    return realize_operation(
        owner.context,
        value.operation,
        operation->preparation.get(),
        std::move(operands)
    );
}

auto BodyRealizer::ExpressionBuilder::emit(
    Recipe& recipe,
    ConstructionUse use,
    ConstantLiteralContext literal
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
    if (copy_binding) {
        return call_expression(
            intrinsic_expression(TargetSymbol::StdAsConst),
            target_expressions(std::move(result))
        );
    }
    if (use == ConstructionUse::ReadBorrow
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
    if ((direct_scalar || use == ConstructionUse::OperandValue || use == ConstructionUse::Consume)
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
