module carven:backend.lowering.body.storage.impl;

import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.visit;
import std;

namespace body_lowering {

auto BodyLowerer::binding_expression(LocalBindingID id) noexcept -> TargetExpr {
    used_bindings.insert(id);
    auto result = name_expression(binding_names.at(id));
    if (const auto* capture = std::get_if<CaptureBindingStorage>(&body.binding(id).storage);
        capture != nullptr && capture->mode == CaptureMode::Write) {
        return call_member(std::move(result), "get", {});
    }
    return delayed_bindings.contains(id) ? dereference_expression(std::move(result))
                                         : std::move(result);
}

auto BodyLowerer::declare_binding(
    LocalBindingID id,
    TargetExpr initializer,
    StatementSequence& destination
) noexcept -> void {
    const auto& binding = body.binding(id);
    const auto& owner = std::get<OwnerBindingStorage>(binding.storage);
    auto type = context.lower_type(binding.type);
    if (delayed_bindings.contains(id)) {
        type = context.optional_type(type);
        initializer = TargetExpr {
            .value = TargetConstructionExpr {
                .type = type,
                .initializer = target_expressions(std::move(initializer))
            }
        };
    }
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding =
                delayed_bindings.contains(id) || owner.writable || taken_bindings.contains(id)
                ? TargetVariableBinding::MutableValue
                : TargetVariableBinding::ConstValue,
            .maybe_unused = true,
            .name = binding_names.at(id),
            .type = type,
            .initializer = std::move(initializer),
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

auto BodyLowerer::known_boolean(const SemanticExpression& source) const noexcept
    -> std::optional<bool> {
    if (!source.constant.has_value()) {
        return std::nullopt;
    }
    const auto& fact = context.semantic().constants().constant(*source.constant);
    const auto* boolean = std::get_if<BooleanConstant>(&fact.value);
    return boolean == nullptr ? std::nullopt : std::optional {boolean->value};
}

auto BodyLowerer::condition(
    const SemanticExpression& source,
    StatementSequence& destination
) noexcept -> std::optional<TargetExpr> {
    auto statements = StatementSequence();
    auto value = expression(source, statements);
    if (!statements.continues()) {
        destination.append(std::move(statements));
        return std::nullopt;
    }
    if (statements.empty()) {
        return value;
    }
    if (context.plan().failure_abi().members(source.failures.resolved()).empty()
        && !source.exits_test) {
        statements.terminate(
            generated_statement(TargetReturnStmt {.expression = std::move(*value)})
        );

        return TargetExpr {
            .value = TargetRegionExpr {
                .result = context.lower_type(source.type.resolved()),
                .body = std::move(statements).finish()
            }
        };
    }
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
    destination.block(std::move(statements));
    return name_expression(result);
}

auto BodyLowerer::initializers(
    std::span<const SemanticExpression* const> sources,
    bool ordered,
    StatementSequence& destination
) noexcept -> std::optional<std::vector<TargetExpr>> {
    auto statements = std::vector<StatementSequence>(sources.size());
    auto values = std::vector<TargetExpr>();
    for (auto index = 0uz; index < sources.size(); ++index) {
        auto value = expression(*sources[index], statements[index]);
        if (!value) {
            break;
        }
        values.push_back(std::move(*value));
    }

    if (ordered && std::ranges::all_of(statements, [](const auto& value) static noexcept {
            return value.empty();
        })) {
        return values;
    }
    for (auto index = 0uz; index < statements.size(); ++index) {
        auto& prefix = statements[index];
        destination.append(std::move(prefix));
        if (!destination.continues()) {
            return std::nullopt;
        }
        const auto name = names.fresh(TargetTemporaryNameKind::Operand);
        destination.emit(source_statement(
            context.semantic(),
            sources[index]->origin,
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = name,
                .type = context.intrinsic_type(TargetSymbol::Auto),
                .initializer = std::move(values[index]),
            }
        ));
        values[index] = transfer_expression(name_expression(name));
    }
    return values;
}

auto BodyLowerer::operand(
    const SemanticExpression& source,
    StatementSequence& destination,
    OperandUse use
) noexcept -> std::optional<TargetExpr> {
    auto value = expression(source, destination);
    if (!value) {
        return value;
    }
    if (use == OperandUse::Direct
        || std::holds_alternative<SemConstant>(source.value)
        || std::holds_alternative<SemCallable>(source.value)
        || std::holds_alternative<SemEnumConstructor>(source.value)) {
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
            .maybe_unused = false,
            .name = name,
            .type = use == OperandUse::Read
                ? context.lower_parameter(
                      CallableParameter {.access = AccessMode::Read, .type = source.type.resolved()}
                  )
                : context.intrinsic_type(TargetSymbol::Auto),
            .initializer = std::move(*value),
        }
    ));
    auto result = name_expression(name);
    return use == OperandUse::Own ? transfer_expression(std::move(result)) : std::move(result);
}


auto BodyLowerer::operands(
    std::span<const Operand> sources,
    StatementSequence& destination
) noexcept -> std::optional<std::vector<TargetExpr>> {
    auto values = std::vector<TargetExpr>();
    for (const auto& source : sources) {
        auto value = operand(source.expression, destination, source.use);
        if (!value) {
            return std::nullopt;
        }
        values.push_back(std::move(*value));
    }
    return values;
}

} // namespace body_lowering
