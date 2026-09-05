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

auto transfer_expression(TargetExpr value) noexcept -> TargetExpr {
    return call_expression(
        intrinsic_expression(TargetSymbol::RuntimeTransfer),
        target_expressions(std::move(value))
    );
}


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
    std::vector<TargetStmt>& destination
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
    destination.push_back(generated_statement(
        TargetVariableStmt {
            .binding =
                delayed_bindings.contains(id) || owner.writable || taken_bindings.contains(id)
                ? TargetVariableBinding::MutableValue
                : TargetVariableBinding::ConstValue,
            .maybe_unused = false,
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

auto BodyLowerer::known_boolean(const SemIRExpression& source) const noexcept
    -> std::optional<bool> {
    if (!source.constant.has_value()) {
        return std::nullopt;
    }
    const auto& fact = context.semantic().constants().constant(*source.constant);
    const auto* boolean = std::get_if<BooleanConstant>(&fact.value);
    return boolean == nullptr ? std::nullopt : std::optional {boolean->value};
}

auto BodyLowerer::condition(
    const SemIRExpression& source,
    std::vector<TargetStmt>& destination
) noexcept -> TargetExpr {
    auto statements = std::vector<TargetStmt>();
    auto value = expression(source, statements);
    if (!falls_through(statements)) {
        destination.insert(
            destination.end(),
            std::make_move_iterator(statements.begin()),
            std::make_move_iterator(statements.end())
        );
        return bool_expression(false);
    }
    if (statements.empty()) {
        return value;
    }
    if (context.plan().failure_abi().members(source.failures).empty() && !source.exits_test) {
        statements.push_back(
            generated_statement(TargetReturnStmt {.expression = std::move(value)})
        );
        mark_unused(statements);
        return {
            .value = TargetRegionExpr {
                .result = context.lower_type(source.type),
                .body = std::move(statements)
            }
        };
    }
    const auto result = names.fresh(TargetTemporaryNameKind::Logic);
    destination.push_back(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = result,
            .type = context.lower_type(source.type),
            .initializer = bool_expression(false)
        }
    ));
    statements.push_back(generated_statement(
        TargetAssignmentStmt {
            .target = name_expression(result),
            .op = TargetAssignmentOperator::Assign,
            .value = std::move(value)
        }
    ));
    destination.push_back(
        generated_statement(TargetBlockStmt {.statements = std::move(statements), .scoped = true})
    );
    return name_expression(result);
}

auto BodyLowerer::initializers(
    std::span<const SemIRExpression* const> sources,
    bool ordered,
    std::vector<TargetStmt>& destination
) noexcept -> std::vector<TargetExpr> {
    auto statements = std::vector<std::vector<TargetStmt>>(sources.size());
    auto values = std::vector<TargetExpr>();
    for (auto index = 0uz; index < sources.size(); ++index) {
        values.push_back(expression(*sources[index], statements[index]));
        if (!falls_through(statements[index])) {
            break;
        }
    }
    if (ordered && std::ranges::all_of(statements, [](const auto& value) static noexcept {
            return value.empty();
        })) {
        return values;
    }
    for (auto index = 0uz; index < values.size(); ++index) {
        auto& prefix = statements[index];
        destination.insert(
            destination.end(),
            std::make_move_iterator(prefix.begin()),
            std::make_move_iterator(prefix.end())
        );
        if (!falls_through(destination)) {
            return {};
        }
        const auto name = names.fresh(TargetTemporaryNameKind::Operand);
        destination.push_back(source_statement(
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
    const SemIRExpression& source,
    std::vector<TargetStmt>& destination,
    OperandUse use
) noexcept -> TargetExpr {
    auto value = expression(source, destination);
    if (!falls_through(destination)) {
        return value;
    }
    if (std::holds_alternative<SemLiteral>(source.value)
        || std::holds_alternative<SemConstant>(source.value)
        || std::holds_alternative<SemCallable>(source.value)
        || std::holds_alternative<SemEnumConstructor>(source.value)) {
        return value;
    }
    const auto name = names.fresh(TargetTemporaryNameKind::Operand);
    destination.push_back(source_statement(
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
                      CallableParameter {.access = AccessMode::Read, .type = source.type}
                  )
                : context.intrinsic_type(TargetSymbol::Auto),
            .initializer = std::move(value),
        }
    ));
    auto result = name_expression(name);
    return use == OperandUse::Own ? transfer_expression(std::move(result)) : std::move(result);
}

auto BodyLowerer::mark_unused_expression(TargetExpr& expression) noexcept -> void {
    std::visit(
        [&](auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, TargetRegionExpr>) {
                mark_unused(value.body);
            } else if constexpr (std::same_as<Value, TargetBinaryExpr>) {
                mark_unused_expression(*value.left);
                mark_unused_expression(*value.right);
            } else if constexpr (std::same_as<Value, TargetConditionalExpr>) {
                mark_unused_expression(*value.condition);
                mark_unused_expression(*value.true_value);
                mark_unused_expression(*value.false_value);
            } else if constexpr (std::same_as<Value, TargetCallExpr>) {
                mark_unused_expression(*value.callee);
                for (auto& argument : value.arguments) {
                    mark_unused_expression(argument);
                }
            } else if constexpr (std::same_as<Value, TargetArrayExpr>) {
                mark_unused_expression(*value.extent);
                for (auto& element : value.elements) {
                    mark_unused_expression(element);
                }
            } else if constexpr (std::same_as<Value, TargetConstructionExpr>) {
                std::visit(
                    Overloaded {
                        [&](std::vector<TargetExpr>& values) noexcept {
                            for (auto& item : values) {
                                mark_unused_expression(item);
                            }
                        },
                        [&](std::vector<TargetFieldInitializer>& fields) noexcept {
                            for (auto& field : fields) {
                                mark_unused_expression(*field.value);
                            }
                        },
                        [](std::monostate&) static noexcept {},
                    },
                    value.initializer
                );
            } else if constexpr (std::same_as<Value, TargetIndexExpr>) {
                mark_unused_expression(*value.operand);
                mark_unused_expression(*value.index);
            } else if constexpr (requires { value.operand; }) {
                mark_unused_expression(*value.operand);
            }
        },
        expression.value
    );
}

auto BodyLowerer::mark_unused(std::vector<TargetStmt>& statements) noexcept -> void {
    const auto mark_variable = [&](TargetVariableStmt& value) noexcept {
        for (const auto& [binding, name] : binding_names) {
            if (name.spelling() == value.name.spelling()) {
                value.maybe_unused = !used_bindings.contains(binding);
            }
        }
        mark_unused_expression(value.initializer);
    };
    const auto mark_clause = [&](auto& clause) noexcept {
        std::visit(
            Overloaded {
                [&](TargetVariableStmt& value) noexcept { mark_variable(value); },
                [&](TargetExprStmt& value) noexcept { mark_unused_expression(value.expression); },
                [&](TargetDiscardStmt& value) noexcept {
                    mark_unused_expression(value.expression);
                },
                [&](TargetAssignmentStmt& value) noexcept {
                    mark_unused_expression(value.target);
                    mark_unused_expression(value.value);
                },
                [&](TargetUpdateStmt& value) noexcept { mark_unused_expression(value.target); },
            },
            clause.value
        );
    };
    for (auto& statement : statements) {
        std::visit(
            Overloaded {
                [&](TargetVariableStmt& value) noexcept { mark_variable(value); },
                [&](TargetExprStmt& value) noexcept { mark_unused_expression(value.expression); },
                [&](TargetDiscardStmt& value) noexcept {
                    mark_unused_expression(value.expression);
                },
                [&](TargetReturnStmt& value) noexcept {
                    if (value.expression.has_value()) {
                        mark_unused_expression(*value.expression);
                    }
                },
                [&](TargetAssignmentStmt& value) noexcept {
                    mark_unused_expression(value.target);
                    mark_unused_expression(value.value);
                },
                [&](TargetUpdateStmt& value) noexcept { mark_unused_expression(value.target); },
                [&](TargetBlockStmt& value) noexcept { mark_unused(value.statements); },
                [&](TargetIfStmt& value) noexcept {
                    for (auto& branch : value.branches) {
                        mark_unused_expression(branch.condition);
                        mark_unused(branch.body);
                    }
                    if (value.else_body.has_value()) {
                        mark_unused(*value.else_body);
                    }
                },
                [&](TargetWhileStmt& value) noexcept {
                    mark_unused_expression(value.condition);
                    mark_unused(value.body);
                },
                [&](TargetRangeForStmt& value) noexcept {
                    for (const auto& [binding, name] : binding_names) {
                        if (name.spelling() == value.name.spelling()) {
                            value.maybe_unused = !used_bindings.contains(binding);
                        }
                    }
                    mark_unused_expression(value.range);
                    mark_unused(value.body);
                },
                [&](TargetForStmt& value) noexcept {
                    if (value.initializer.has_value()) {
                        mark_clause(*value.initializer);
                    }
                    if (value.condition.has_value()) {
                        mark_unused_expression(*value.condition);
                    }
                    for (auto& step : value.steps) {
                        mark_clause(step);
                    }
                    mark_unused(value.body);
                },
                [](auto&) static noexcept {},
            },
            statement.value
        );
    }
}

} // namespace body_lowering
