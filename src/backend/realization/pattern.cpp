module carven:backend.realization.pattern.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.constant;
import :backend.realization.pattern;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto PatternRealizer::subject_expression(const PatternSubject& subject) noexcept -> TargetExpr {
    auto result = name_expression(subject.root);
    if (subject.dereference_root) {
        result = dereference_expression(std::move(result));
    }
    if (subject.payload_index) {
        result = member_expression(
            std::move(result),
            TargetNameAllocator::enum_payload_field(*subject.payload_index)
        );
    }
    return result;
}

auto PatternRealizer::prepare(
    std::span<const PatternBindingType> bindings,
    LoweringStmtBuilder& destination
) noexcept -> PatternState {
    auto state =
        PatternState {.matched = names.fresh(TargetTemporaryNameKind::Logic), .addresses = {}};
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = state.matched,
            .type = context.intrinsic_type(TargetSymbol::Bool),
            .initializer = bool_expression(false)
        }
    ));
    for (const auto& binding : bindings) {
        const auto address = names.fresh(TargetTemporaryNameKind::Operand);
        if (!state.addresses.emplace(binding.binding, address).second) {
            invariant_violation("pattern arm lists the same binding more than once");
        }
        destination.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = address,
                .type = context.pointer_type(context.target().intern_type(
                    {.value =
                         TargetIntrinsicType {
                             .symbol = TargetSymbol::StdAddConst,
                             .type_argument_ids = {context.lower_type(binding.type)}
                         },
                     .const_qualified = false}
                )),
                .initializer = intrinsic_expression(TargetSymbol::StdNullptr)
            }
        ));
    }
    return state;
}

auto PatternRealizer::branch(
    TargetExpr condition,
    LoweringStmtBuilder selected,
    LoweringStmtBuilder& destination
) noexcept -> void {
    destination.record_exits(selected.exits());
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({.condition = std::move(condition), .body = std::move(selected).finish()});
    destination.emit(generated_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
}

auto PatternRealizer::match(
    PatternID pattern_id,
    const PatternSubject& subject,
    const PatternState& state,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto& pattern = body.pattern(pattern_id);
    const auto set = [&](TargetExpr value) noexcept {
        destination.emit(generated_statement(
            TargetAssignmentStmt {
                .target = name_expression(state.matched),
                .op = TargetAssignmentOperator::Assign,
                .value = std::move(value)
            }
        ));
    };
    std::visit(
        Overloaded {
            [&](const WildcardPattern&) noexcept { set(bool_expression(true)); },
            [&](const LiteralPattern& value) noexcept {
                set(binary_expression(
                    subject_expression(subject),
                    TargetBinaryOperator::Equal,
                    constant_expression(context, value.constant)
                ));
            },
            [&](const TypeConstraintPattern& value) noexcept {
                if (value.type != pattern.type) {
                    invariant_violation("concrete type-constraint pattern changed its type");
                }
                set(bool_expression(true));
            },
            [&](const BindingPattern& value) noexcept {
                destination.emit(generated_statement(
                    TargetAssignmentStmt {
                        .target = name_expression(state.addresses.at(value.binding)),
                        .op = TargetAssignmentOperator::Assign,
                        .value = call_expression(
                            intrinsic_expression(TargetSymbol::StdAddressof),
                            target_expressions(subject_expression(subject))
                        )
                    }
                ));
                set(bool_expression(true));
            },
            [&](const OrPattern& value) noexcept {
                set(bool_expression(false));
                for (const auto alternative_id : value.alternatives) {
                    auto candidate = LoweringStmtBuilder();
                    match(alternative_id, subject, state, candidate);
                    branch(
                        prefix_expression(
                            TargetPrefixOperator::LogicalNot,
                            name_expression(state.matched)
                        ),
                        std::move(candidate),
                        destination
                    );
                }
            },
            [&](const EnumCasePattern& value) noexcept {
                const auto& declaration =
                    context.semantic().declarations().enum_case(value.enum_case);
                if (declaration.payload_types.size() != value.payload.size()) {
                    invariant_violation("enum pattern payload does not match its declaration");
                }
                if (!std::holds_alternative<PayloadEnumRepresentation>(
                        context.semantic()
                            .declarations()
                            .enumeration(declaration.owner)
                            .representation
                    )) {
                    set(binary_expression(
                        subject_expression(subject),
                        TargetBinaryOperator::Equal,
                        enum_case_expression(context, value.enum_case, {})
                    ));
                    return;
                }
                const auto projection = names.fresh(TargetTemporaryNameKind::SuccessProjection);
                const auto ordinal = enum_case_index(context.semantic(), value.enum_case);
                destination.emit(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = projection,
                        .type =
                            context.pointer_type(context.intrinsic_type(TargetSymbol::Auto, true)),
                        .initializer = call_member(
                            subject_expression(subject),
                            context.payload_enum(declaration.owner)
                                .cases[ordinal]
                                .projection_function.spelling(),
                            {}
                        )
                    }
                ));
                set(binary_expression(
                    name_expression(projection),
                    TargetBinaryOperator::NotEqual,
                    intrinsic_expression(TargetSymbol::StdNullptr)
                ));
                for (auto index = 0uz; index < value.payload.size(); ++index) {
                    auto child = LoweringStmtBuilder();
                    match(
                        value.payload[index],
                        {.root = projection,
                         .dereference_root = true,
                         .payload_index = static_cast<std::uint32_t>(index)},
                        state,
                        child
                    );
                    branch(name_expression(state.matched), std::move(child), destination);
                }
            }
        },
        pattern.value
    );
}
