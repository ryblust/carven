module carven:backend.lowering.body.storage.impl;

import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.type;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyLowerer::binding_expression(LocalBindingID id) noexcept -> TargetExpr {
    auto result = name_expression(binding_names.at(id));
    if (const auto* capture = std::get_if<CaptureBindingStorage>(&body.binding(id).storage);
        capture != nullptr && capture->mode == CaptureMode::Write) {
        return call_member(std::move(result), "get", {});
    }
    if (const auto found = delayed_bindings.find(id); found != delayed_bindings.end()) {
        return dereference_expression(name_expression(found->second.name));
    }
    return result;
}

auto BodyLowerer::declare_binding(
    LocalBindingID id,
    TargetExpr initializer,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto& binding = body.binding(id);
    const auto& owner = std::get<OwnerBindingStorage>(binding.storage);
    const auto type = context.lower_type(binding.type);
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = owner.writable || taken_bindings.contains(id)
                ? TargetVariableBinding::MutableValue
                : TargetVariableBinding::ConstValue,
            .maybe_unused = true,
            .name = binding_names.at(id),
            .type = type,
            .initializer = std::move(initializer),
        }
    ));
}

auto BodyLowerer::declare_deferred(
    const LoweringDeferredStorage& storage,
    bool maybe_unused,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto type = context.target().intern_type(
        {.value =
             TargetIntrinsicType {
                 .symbol = TargetSymbol::RuntimeDeferredStorage,
                 .type_argument_ids = {storage.value_type}
             },
         .const_qualified = false}
    );
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = maybe_unused,
            .name = storage.name,
            .type = type,
            .initializer = TargetExpr {
                .value =
                    TargetConstructionExpr {.type = type, .initializer = std::vector<TargetExpr>()}
            }
        }
    ));
}

auto BodyLowerer::field_identifier(StructID owner, std::uint32_t index) noexcept
    -> TargetIdentifier {
    const auto& structure = context.semantic().declarations().structure(owner);
    return context.name_allocator().source(
        context.semantic().provenance().spelling(structure.fields[index].name),
        context.semantic().provenance().spelling(structure.name)
    );
}

auto BodyLowerer::condition(const SemanticExpression& source) noexcept
    -> Lowered<LoweringPredicate> {
    auto destination = LoweringStmtBuilder();
    auto result = [&]() noexcept -> std::optional<LoweringPredicate> {
        auto statements = LoweringStmtBuilder();
        if (const auto known = ::known_boolean(context.semantic(), source)) {
            static_cast<void>(statements.accept(retain_evaluation(source)));
            if (!statements.empty()) {
                destination.scope(std::move(statements));
            }
            if (!destination.continues()) {
                return std::nullopt;
            }
            return LoweringKnownBool {*known};
        }
        if (evaluation_form(source) == EvaluationForm::Branches) {
            const auto result = names.fresh(TargetTemporaryNameKind::Logic);
            consume_expression(
                source,
                LoweringLiteralContext::Exact,
                ResultDemand::Observe,
                [&](LoweringResult value, LoweringStmtBuilder& branch) noexcept {
                    branch.emit(generated_statement(
                        TargetAssignmentStmt {
                            .target = name_expression(result),
                            .op = TargetAssignmentOperator::Assign,
                            .value =
                                require_expression(std::move(value), LoweringResultUse::Observe)
                        }
                    ));
                },
                statements
            );
            if (!statements.continues()) {
                destination.scope(std::move(statements));
                return std::nullopt;
            }
            destination.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .name = result,
                    .type = context.lower_type(source.type.resolved()),
                    .initializer = bool_expression(false)
                }
            ));
            destination.scope(std::move(statements));
            return destination.continues()
                ? std::optional<LoweringPredicate>(LoweringDynamicBool {name_expression(result)})
                : std::nullopt;
        }
        auto value = read_value(full_expression(source), statements);
        if (!statements.continues()) {
            destination.append(std::move(statements));
            return std::nullopt;
        }
        if (!statements.empty()
            && evaluation_preserves_full_expression(context.semantic(), source)) {
            const auto result = names.fresh(TargetTemporaryNameKind::Logic);
            destination.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .name = result,
                    .type = context.lower_type(source.type.resolved()),
                    .initializer = bool_expression(false)
                }
            ));
            statements.emit(generated_statement(
                TargetAssignmentStmt {
                    .target = name_expression(result),
                    .op = TargetAssignmentOperator::Assign,
                    .value = std::move(*value)
                }
            ));
            destination.scope(std::move(statements));
            return LoweringDynamicBool {name_expression(result)};
        }
        destination.append(std::move(statements));
        return LoweringDynamicBool {std::move(*value)};
    }();
    return std::move(destination).complete<LoweringPredicate>(std::move(result));
}

auto BodyLowerer::materialize_operand(
    const SemanticExpression& source,
    TargetExpr value,
    OperandUse use,
    LoweringStmtBuilder& destination
) noexcept -> TargetExpr {
    if (!evaluation_requires_execution(context.semantic(), source)
        && !evaluation_reads_storage(context.semantic(), source)) {
        return value;
    }
    const auto name = names.fresh(TargetTemporaryNameKind::Operand);
    destination.emit(source_statement(
        context.semantic(),
        source.origin,
        TargetVariableStmt {
            .binding = use == OperandUse::Place ? TargetVariableBinding::RvalueReference
                : use == OperandUse::ConstPlace ? TargetVariableBinding::ConstReference
                : use == OperandUse::Snapshot   ? TargetVariableBinding::ConstValue
                                                : TargetVariableBinding::MutableValue,
            .maybe_unused = true,
            .name = name,
            .type = use == OperandUse::Read
                ? context.lower_parameter(
                      CallableParameter {.access = AccessMode::Read, .type = source.type.resolved()}
                  )
                : context.intrinsic_type(TargetSymbol::Auto),
            .initializer = std::move(value)
        }
    ));
    auto result = name_expression(name);
    return use == OperandUse::Own ? transfer_expression(std::move(result)) : std::move(result);
}

auto BodyLowerer::operand(
    const SemanticExpression& source,
    OperandUse use,
    LoweringLiteralContext literal
) noexcept -> Lowered<TargetExpr> {
    auto destination = LoweringStmtBuilder();
    auto value = read_value(
        expression(
            source,
            use == OperandUse::Read ? literal : LoweringLiteralContext::Exact,
            use == OperandUse::Read || use == OperandUse::ConstPlace ? ResultDemand::Observe
                                                                     : ResultDemand::Value
        ),
        destination,
        use == OperandUse::Own || use == OperandUse::Snapshot ? LoweringResultUse::Transfer
                                                              : LoweringResultUse::Observe
    );
    if (value) {
        value = materialize_operand(source, std::move(*value), use, destination);
    }
    return std::move(destination).complete<TargetExpr>(std::move(value));
}
