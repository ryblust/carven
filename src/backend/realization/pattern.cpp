module carven:backend.realization.pattern.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.composition;
import :backend.realization.pattern;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :support.invariant;
import :support.task;
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
) noexcept -> PatternBindings {
    auto result = PatternBindings {.addresses = {}};
    for (const auto& binding : bindings) {
        const auto address =
            context.target().add_local(names.fresh(TargetTemporaryNameKind::Operand));
        if (!result.addresses.emplace(binding.binding, address).second) {
            invariant_violation("pattern arm lists the same binding more than once");
        }
        destination.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = address,
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
    return result;
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

auto PatternRealizer::combine(
    ShortCircuitOperator operation,
    LoweringPredicate left,
    Lowered<LoweringPredicate> right,
    LoweringStmtBuilder& destination
) noexcept -> std::optional<LoweringPredicate> {
    const auto conjunction = operation == ShortCircuitOperator::And;
    if (const auto* known = std::get_if<LoweringKnownBool>(&left)) {
        if (known->value != conjunction) {
            return left;
        }
        return destination.accept(std::move(right));
    }
    if (right.statements.empty() && right.normal) {
        if (const auto* known = std::get_if<LoweringKnownBool>(&*right.normal);
            known != nullptr && known->value == conjunction) {
            return left;
        }
        return LoweringDynamicBool {binary_expression(
            predicate_expression(std::move(left)),
            conjunction ? TargetBinaryOperator::LogicalAnd : TargetBinaryOperator::LogicalOr,
            predicate_expression(std::move(*right.normal))
        )};
    }
    const auto local = context.target().add_local(names.fresh(TargetTemporaryNameKind::Logic));
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .local = local,
            .type = context.intrinsic_type(TargetSymbol::Bool),
            .initializer = predicate_expression(std::move(left))
        }
    ));
    auto selected = LoweringStmtBuilder();
    auto predicate = selected.accept(std::move(right));
    if (predicate) {
        selected.emit(generated_statement(
            TargetAssignmentStmt {
                .target = name_expression(local),
                .op = TargetAssignmentOperator::Assign,
                .value = predicate_expression(std::move(*predicate))
            }
        ));
    }
    auto condition = name_expression(local);
    if (!conjunction) {
        condition = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(condition));
    }
    branch(std::move(condition), std::move(selected), destination);
    return LoweringDynamicBool {name_expression(local)};
}

auto PatternRealizer::match(
    PatternID pattern_id,
    const PatternSubject& subject,
    const PatternBindings& bindings
) noexcept -> ContinuationTask<Lowered<LoweringPredicate>> {
    const auto& pattern = body.pattern(pattern_id);
    auto destination = LoweringStmtBuilder();
    auto predicate = co_await pattern.value.visit(
        Overloaded {
            [](const WildcardPattern&) static noexcept
                -> ContinuationTask<std::optional<LoweringPredicate>> {
                co_return LoweringKnownBool {true};
            },
            [&](const RangePattern& value) noexcept
                -> ContinuationTask<std::optional<LoweringPredicate>> {
                const auto read =
                    [&](const std::optional<RangePatternBound>& part,
                        bool upper) noexcept -> ContinuationTask<std::optional<TargetExpr>> {
                    if (!part) {
                        co_return std::nullopt;
                    }
                    if (part->constant) {
                        co_return constant_expression(context, *part->constant);
                    }
                    if (!bound) {
                        invariant_violation("dynamic range pattern has no bound realizer");
                    }
                    co_return (co_await bound(pattern_id, upper, destination));
                };
                auto first = (co_await read(value.begin, false));
                if (!destination.continues()) {
                    co_return std::nullopt;
                }
                auto last = (co_await read(value.end, true));
                if (!destination.continues()) {
                    co_return std::nullopt;
                }
                auto test = std::optional<TargetExpr>();
                if (first) {
                    test = binary_expression(
                        subject_expression(subject),
                        TargetBinaryOperator::GreaterEqual,
                        std::move(*first)
                    );
                }
                if (last) {
                    auto upper = binary_expression(
                        subject_expression(subject),
                        value.inclusive ? TargetBinaryOperator::LessEqual
                                        : TargetBinaryOperator::Less,
                        std::move(*last)
                    );
                    test = test ? binary_expression(
                                      std::move(*test),
                                      TargetBinaryOperator::LogicalAnd,
                                      std::move(upper)
                                  )
                                : std::move(upper);
                }
                if (!test) {
                    co_return LoweringKnownBool {true};
                }
                co_return LoweringDynamicBool {std::move(*test)};
            },
            [&](const LiteralPattern& value) noexcept
                -> ContinuationTask<std::optional<LoweringPredicate>> {
                co_return LoweringDynamicBool {binary_expression(
                    subject_expression(subject),
                    TargetBinaryOperator::Equal,
                    constant_expression(context, value.constant)
                )};
            },
            [&](const TypeConstraintPattern& value) noexcept
                -> ContinuationTask<std::optional<LoweringPredicate>> {
                if (value.type != pattern.type) {
                    invariant_violation("concrete type-constraint pattern changed its type");
                }
                co_return LoweringKnownBool {true};
            },
            [&](const BindingPattern& value) noexcept
                -> ContinuationTask<std::optional<LoweringPredicate>> {
                destination.emit(generated_statement(
                    TargetAssignmentStmt {
                        .target = name_expression(bindings.addresses.at(value.binding)),
                        .op = TargetAssignmentOperator::Assign,
                        .value = call_expression(
                            intrinsic_expression(TargetSymbol::StdAddressof),
                            target_expressions(subject_expression(subject))
                        )
                    }
                ));
                co_return LoweringKnownBool {true};
            },
            [&](const OrPattern& value) noexcept
                -> ContinuationTask<std::optional<LoweringPredicate>> {
                auto result = std::optional<LoweringPredicate>(LoweringKnownBool {false});
                for (const auto alternative : value.alternatives) {
                    if (!result || known_predicate(result) == true) {
                        break;
                    }
                    auto candidate = co_await match(alternative, subject, bindings);
                    result = combine(
                        ShortCircuitOperator::Or,
                        std::move(*result),
                        std::move(candidate),
                        destination
                    );
                }
                co_return result;
            },
            [&](const EnumCasePattern& value) noexcept
                -> ContinuationTask<std::optional<LoweringPredicate>> {
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
                    co_return LoweringDynamicBool {binary_expression(
                        subject_expression(subject),
                        TargetBinaryOperator::Equal,
                        enum_case_expression(context, value.enum_case, {})
                    )};
                }
                const auto projection = context.target().add_local(
                    names.fresh(TargetTemporaryNameKind::SuccessProjection)
                );
                const auto ordinal = enum_case_index(context.semantic(), value.enum_case);
                destination.emit(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::ConstValue,
                        .maybe_unused = false,
                        .local = projection,
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
                auto result =
                    std::optional<LoweringPredicate>(LoweringDynamicBool {binary_expression(
                        name_expression(projection),
                        TargetBinaryOperator::NotEqual,
                        intrinsic_expression(TargetSymbol::StdNullptr)
                    )});
                for (auto index = 0uz; index < value.payload.size(); ++index) {
                    if (!result || known_predicate(result) == false) {
                        break;
                    }
                    auto child = co_await match(
                        value.payload[index],
                        {.root = projection,
                         .dereference_root = true,
                         .payload_index = static_cast<std::uint32_t>(index)},
                        bindings
                    );
                    result = combine(
                        ShortCircuitOperator::And,
                        std::move(*result),
                        std::move(child),
                        destination
                    );
                }
                co_return result;
            }
        }
    );
    co_return std::move(destination).complete<LoweringPredicate>(std::move(predicate));
}

PatternRealizer::PatternRealizer(
    ModuleLowering& context,
    TargetNameAllocator& names,
    const SemIRBody& body,
    PatternBoundRealizer bound
) noexcept
    : context(context),
      names(names),
      body(body),
      bound(std::move(bound)) {}
