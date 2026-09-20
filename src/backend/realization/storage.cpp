module carven:backend.realization.storage.impl;

import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.expr;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.realization.report;
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

auto BodyRealizer::ExpressionBuilder::retain_input(Fragment& fragment, PreparedUse use) noexcept
    -> void {
    if (borrowed_owner(source(fragment), use)
        && !std::holds_alternative<SemBinding>(source(fragment).operation.value)
        && !std::holds_alternative<SemCallable>(source(fragment).operation.value)) {
        anchor(fragment, use, true);
    }
}

auto BodyRealizer::ExpressionBuilder::raw(
    Fragment& fragment,
    ConstantLiteralContext literal
) noexcept -> TargetExpr {
    if (auto* value = std::get_if<TargetExpr>(&fragment.completion)) {
        auto result = std::move(*value);
        complete(fragment, LoweringCompleted {});
        return result;
    }
    if (const auto* value = saved(fragment)) {
        auto expression = name_expression(value->local);
        if (value->kind == SavedKind::StoredValue
            || value->kind == SavedKind::StoredPlace
            || value->kind == SavedKind::Success) {
            expression = dereference_expression(std::move(expression));
        }
        if (value->kind == SavedKind::Success) {
            expression =
                member_expression(std::move(expression), TargetIdentifier::from_spelling("value"));
        }
        return expression;
    }
    if (const auto* binding = std::get_if<LocalBindingID>(&fragment.completion)) {
        return owner.binding_expression(*binding);
    }
    if (const auto* constant = std::get_if<ConstantID>(&fragment.completion)) {
        return constant_expression(owner.context, *constant, literal);
    }
    invariant_violation("completed fragment has no residual expression");
}

auto BodyRealizer::ExpressionBuilder::emit(
    Fragment& fragment,
    PreparedUse use,
    ConstantLiteralContext literal
) noexcept -> TargetExpr {
    if (use == PreparedUse::ProjectionPlace) {
        invariant_violation("projection access was not resolved before realization");
    }
    if (!saved(fragment) && (use == PreparedUse::WritePlace || use == PreparedUse::NativeTake)) {
        if (const auto* binding = std::get_if<LocalBindingID>(&fragment.completion);
            binding != nullptr
            && std::holds_alternative<OwnerBindingStorage>(
                owner.metadata.binding(*binding).storage
            )) {
            owner.mutable_owners.emplace(owner.binding_locals.at(*binding));
        }
    }
    if (use == PreparedUse::NativeTake) {
        // The query promises T&&. Do not first turn a trivial Take into
        // const T& via Carven transfer and then cast away constness.
        return native_take_expression(
            owner.context,
            source(fragment).operation.type.resolved(),
            raw(fragment, literal)
        );
    }
    auto result = raw(fragment, literal);
    if (use == PreparedUse::Consume
        && saved(fragment)
        && saved(fragment)->kind != SavedKind::Place
        && saved(fragment)->kind != SavedKind::StoredPlace) {
        if (saved(fragment)->kind == SavedKind::Success
            && scalar(source(fragment).operation.type.resolved())) {
            // Scalar transfer observes const T&; the success projection
            // already promises const access and cannot call transfer(T&).
            return TargetExpr {
                .value = TargetStaticCastExpr {
                    .type = owner.context.reference_type(
                        owner.context.lower_type(source(fragment).operation.type.resolved()),
                        true
                    ),
                    .operand = target_child(std::move(result))
                }
            };
        }
        return transfer_expression(std::move(result));
    }
    const auto& value = source(fragment);
    const auto copy_binding = (use == PreparedUse::Consume || use == PreparedUse::OperandValue)
        && !saved(fragment)
        && std::holds_alternative<LocalBindingID>(fragment.completion)
        && !scalar(value.operation.type.resolved());
    // Named values copy even at C++ automatic-move return sites.
    if (copy_binding) {
        return call_expression(
            intrinsic_expression(TargetSymbol::StdAsConst),
            target_expressions(std::move(result))
        );
    }
    if (use == PreparedUse::ReadBorrow
        || use == PreparedUse::ConstPlace
        || use == PreparedUse::AddressValue) {
        const auto type = owner.context.lower_type(source(fragment).operation.type.resolved());
        if (use == PreparedUse::AddressValue && !saved(fragment)) {
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

auto BodyRealizer::ExpressionBuilder::value_binding(PreparedUse use) noexcept
    -> TargetVariableBinding {
    switch (use) {
        case PreparedUse::ReadBorrow:
        case PreparedUse::AddressValue:
        case PreparedUse::OperandValue:
        case PreparedUse::ConstPlace:   return TargetVariableBinding::ConstValue;
        case PreparedUse::WritePlace:
        case PreparedUse::Consume:
        case PreparedUse::NativeTake:   return TargetVariableBinding::MutableValue;
        case PreparedUse::ProjectionPlace:
            invariant_violation("projection access was not resolved before storage");
    }
    std::unreachable();
}

auto BodyRealizer::ExpressionBuilder::anchor(
    Fragment& fragment,
    PreparedUse use,
    bool force,
    bool direct_scalar
) noexcept -> void {
    if (!pending(fragment)) {
        return;
    }
    const auto& value = source(fragment);
    if (!force && !fragment.observes && !fragment.executes) {
        return;
    }
    if (value.operation.category == SemanticValueCategory::Value
        && !scalar(value.operation.type.resolved())
        && !std::holds_alternative<SemBinding>(value.operation.value)
        && value.operation.lifetime != cleanup) {
        invariant_violation("owner anchoring requires its source cleanup frame");
    }
    if (stable_place_binding(value.operation)
        && (use == PreparedUse::WritePlace || use == PreparedUse::ConstPlace)) {
        return;
    }
    const auto name = owner.fresh_local(TargetTemporaryNameKind::Owner);
    if ((direct_scalar || use == PreparedUse::OperandValue || use == PreparedUse::Consume)
        && scalar(value.operation.type.resolved())
        && use != PreparedUse::WritePlace
        && use != PreparedUse::ConstPlace
        && std::holds_alternative<BuiltinTypeValue>(
            owner.context.semantic().types().type(value.operation.type.resolved()).value
        )) {
        fragment.statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = value_binding(use),
                .maybe_unused = false,
                .local = name,
                .type = owner.context.lower_type(value.operation.type.resolved()),
                .initializer = raw(fragment)
            }
        ));
        complete(fragment, Saved {.local = name, .kind = SavedKind::Value});
        return;
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
        && !owner.context.semantic()
                .type_contents(value.operation.type.resolved())
                .read_borrows_storage();
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
    auto storage_type = type;
    const auto exact_call = std::holds_alternative<SemCppCall>(value.operation.value)
        && use != PreparedUse::Consume
        && use != PreparedUse::NativeTake;
    if (exact_call) {
        const auto* native = std::get_if<CppTypeValue>(
            &owner.context.semantic().types().type(value.operation.type.resolved()).value
        );
        const auto* query = native == nullptr ? nullptr : std::get_if<CppQueryType>(&native->form);
        if (query == nullptr) {
            invariant_violation("native call has no result query");
        }
        storage_type = owner.context.lower_cpp_query(*query);
    }
    auto initializer = !exact_call
            && (use == PreparedUse::Consume
                || use == PreparedUse::OperandValue
                || use == PreparedUse::WritePlace
                || use == PreparedUse::NativeTake)
        ? emit(fragment, use)
        : raw(fragment);
    if (automatic_storage) {
        // References and ReadArg already carry access in their type. Owned
        // snapshots instead derive their qualification from the consumer.
        const auto access_in_type = place || (read && !read_value);
        fragment.statements.emit(generated_statement(
            TargetVariableStmt {
                .binding =
                    access_in_type ? TargetVariableBinding::MutableValue : value_binding(use),
                .maybe_unused = false,
                .local = name,
                .type = storage_type,
                .initializer = std::move(initializer)
            }
        ));
        complete(
            fragment,
            Saved {.local = name, .kind = place ? SavedKind::Place : SavedKind::Value}
        );
        return;
    }
    const auto storage = LoweringDeferredStorage {.local = name, .value_type = storage_type};
    owner.declare_deferred(storage, false, fragment.declarations);
    owner.initialize_deferred(
        storage,
        std::move(initializer),
        fragment.statements,
        exact_call ? std::optional(owner.context.intrinsic_type(TargetSymbol::DecltypeAuto))
                   : std::nullopt
    );
    complete(
        fragment,
        Saved {.local = name, .kind = place ? SavedKind::StoredPlace : SavedKind::StoredValue}
    );
}
