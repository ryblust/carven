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
import :semantic.semir.completion;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :support.invariant;
import :support.task;
import :support.visit;
import std;

auto PatternRealizer::single_case(EnumCaseID id) const noexcept -> bool {
    const auto& declarations = context.semantic().declarations();
    return declarations.enumeration(declarations.enum_case(id).owner).cases.size() == 1uz;
}

auto PatternRealizer::projection_locals() const noexcept -> std::span<const TargetLocalID> {
    return projections;
}

auto PatternRealizer::subject_expression(const PatternSubject& subject) noexcept -> TargetExpr {
    auto result = name_expression(subject.root);
    if (subject.dereference_root) {
        result = dereference_expression(std::move(result));
    }
    if (subject.payload_index) {
        result = member_expression(
            std::move(result),
            enum_payload_field_identifier(*subject.payload_index)
        );
    }
    return result;
}

auto PatternRealizer::test(Lowered<LoweringPredicate> predicate) noexcept -> PatternSelection {
    const auto known = known_predicate(predicate.normal);
    auto result = PatternSelection {
        .tests = {},
        .bindings = {},
        .source_locals = {},
        .accepted = predicate.normal.has_value() && known != false,
        .rejected = predicate.normal.has_value() && known != true,
    };
    if (!predicate.statements.empty() || !predicate.exits.entries.empty() || known != true) {
        result.tests.push_back(std::move(predicate));
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

auto PatternRealizer::sequence(PatternSelection left, PatternSelection right) noexcept
    -> PatternSelection {
    if (!left.accepted) {
        return left;
    }
    left.accepted = right.accepted;
    left.rejected |= right.rejected;
    if (left.bindings.size() < right.bindings.size()) {
        left.bindings.swap(right.bindings);
    }
    left.bindings.merge(right.bindings);
    if (!right.bindings.empty()) {
        invariant_violation("pattern success path defines the same binding more than once");
    }
    if (left.source_locals.size() < right.source_locals.size()) {
        left.source_locals.swap(right.source_locals);
    }
    left.source_locals.merge(right.source_locals);
    if (!left.tests.empty() && !right.tests.empty()) {
        auto& previous = left.tests.back();
        auto& next = right.tests.front();
        if (previous.normal && next.normal && next.statements.empty()) {
            if (known_predicate(previous.normal) == true) {
                previous.normal = std::move(next.normal);
            } else if (known_predicate(next.normal) != true) {
                previous.normal = LoweringDynamicBool {binary_expression(
                    predicate_expression(std::move(*previous.normal)),
                    TargetBinaryOperator::LogicalAnd,
                    predicate_expression(std::move(*next.normal))
                )};
            }
            previous.exits.merge(next.exits);
            right.tests.pop_front();
        }
    }
    left.tests.splice(left.tests.end(), right.tests);
    return left;
}

auto PatternRealizer::select(PatternSelection selection, LoweringStmtBuilder accepted) noexcept
    -> LoweringStmtBuilder {
    const auto continues = selection.rejected || (selection.accepted && accepted.continues());
    if (!selection.accepted) {
        accepted = LoweringStmtBuilder();
    }
    for (auto item = selection.tests.rbegin(); item != selection.tests.rend(); ++item) {
        auto destination = LoweringStmtBuilder();
        auto predicate = destination.accept(std::move(*item));
        if (predicate) {
            const auto known = known_predicate(predicate);
            if (known == true) {
                destination.append(std::move(accepted));
            } else if (known != false) {
                branch(
                    predicate_expression(std::move(*predicate)),
                    std::move(accepted),
                    destination
                );
            }
        }
        accepted = std::move(destination);
    }
    if (!continues && accepted.continues()) {
        accepted.terminate(
            generated_statement(
                TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
            ),
            LoweringExitTarget {LoweringExitKind::Unreachable, 0}
        );
    }
    return accepted;
}

auto PatternRealizer::alternatives(std::vector<PatternSelection> choices) noexcept
    -> PatternSelection {
    const auto complete =
        std::ranges::find_if(choices, [](const PatternSelection& choice) static noexcept {
            return !choice.rejected;
        });
    if (complete != choices.end()) {
        choices.erase(std::next(complete), choices.end());
    }
    if (choices.empty()) {
        return test(
            std::move(LoweringStmtBuilder()).complete<LoweringPredicate>(LoweringKnownBool {false})
        );
    }
    if (choices.size() == 1uz) {
        return std::move(choices.front());
    }
    auto result = PatternSelection {
        .tests = {},
        .bindings = {},
        .source_locals = {},
        .accepted = false,
        .rejected = true,
    };
    auto joined = std::set<LocalBindingID>();
    auto first_accepted = true;
    for (const auto& choice : choices) {
        result.rejected = choice.rejected;
        if (!choice.accepted) {
            continue;
        }
        for (const auto& [binding, subject] : choice.bindings) {
            if (choice.source_locals.contains(subject.root)) {
                joined.insert(binding);
            }
        }
        result.accepted = true;
        if (first_accepted) {
            result.bindings = choice.bindings;
            first_accepted = false;
            continue;
        }
        if (choice.bindings.size() != result.bindings.size()) {
            invariant_violation("pattern alternatives define different bindings");
        }
        for (const auto& [binding, subject] : choice.bindings) {
            if (result.bindings.at(binding) != subject) {
                joined.insert(binding);
            }
        }
    }
    // Predicate-only alternatives remain one native short-circuit expression.
    // Existing place identities can also be shared without selection storage.
    const auto direct = joined.empty()
        && std::ranges::all_of(choices, [](const PatternSelection& choice) static noexcept {
                            return choice.tests.empty()
                                || (choice.tests.size() == 1uz
                                    && choice.tests.front().statements.empty()
                                    && choice.tests.front().exits.entries.empty()
                                    && choice.tests.front().normal.has_value());
                        });
    if (direct) {
        // With no evaluation prefix or source choice, guaranteed acceptance
        // discharges the whole predicate rather than retaining `test || true`.
        if (!result.rejected) {
            return result;
        }
        auto predicate = LoweringPredicate(LoweringKnownBool {false});
        for (auto& choice : choices) {
            auto next = choice.tests.empty() ? LoweringPredicate(LoweringKnownBool {true})
                                             : std::move(*choice.tests.front().normal);
            if (const auto* known = std::get_if<LoweringKnownBool>(&predicate)) {
                if (known->value) {
                    break;
                }
                predicate = std::move(next);
            } else {
                predicate = LoweringDynamicBool {binary_expression(
                    predicate_expression(std::move(predicate)),
                    TargetBinaryOperator::LogicalOr,
                    predicate_expression(std::move(next))
                )};
            }
        }
        auto evaluation =
            std::move(LoweringStmtBuilder()).complete<LoweringPredicate>(std::move(predicate));
        result.tests.push_back(std::move(evaluation));
        return result;
    }
    auto destination = LoweringStmtBuilder();
    for (const auto binding : joined) {
        const auto address =
            context.target().add_local(names.fresh(TargetTemporaryNameKind::Operand));
        destination.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = address,
                .type = context.pointer_type(context.target().intern_type(
                    {.value =
                         TargetIntrinsicType {
                             .symbol = TargetSymbol::StdAddConst,
                             .type_argument_ids = {context.lower_type(body.binding(binding).type)}
                         },
                     .const_qualified = false}
                )),
                .initializer = intrinsic_expression(TargetSymbol::StdNullptr)
            }
        ));
        result.bindings.at(
            binding
        ) = {.root = address, .dereference_root = true, .payload_index = std::nullopt};
        result.source_locals.insert(address);
    }
    const auto selected = context.target().add_local(names.fresh(TargetTemporaryNameKind::Logic));
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .local = selected,
            .type = context.intrinsic_type(TargetSymbol::Bool),
            .initializer = bool_expression(false)
        }
    ));
    auto first = true;
    for (auto& choice : choices) {
        auto accepted = LoweringStmtBuilder();
        if (choice.accepted) {
            for (const auto binding : joined) {
                accepted.emit(generated_statement(
                    TargetAssignmentStmt {
                        .target = name_expression(result.bindings.at(binding).root),
                        .op = TargetAssignmentOperator::Assign,
                        .value = call_expression(
                            intrinsic_expression(TargetSymbol::StdAddressof),
                            target_expressions(subject_expression(choice.bindings.at(binding)))
                        )
                    }
                ));
            }
            accepted.emit(generated_statement(
                TargetAssignmentStmt {
                    .target = name_expression(selected),
                    .op = TargetAssignmentOperator::Assign,
                    .value = bool_expression(true)
                }
            ));
        }
        auto attempt = select(std::move(choice), std::move(accepted));
        if (first) {
            destination.scope(std::move(attempt));
            first = false;
        } else {
            branch(
                prefix_expression(TargetPrefixOperator::LogicalNot, name_expression(selected)),
                std::move(attempt),
                destination
            );
        }
    }
    if (!result.accepted && !result.rejected && destination.continues()) {
        destination.terminate(
            generated_statement(
                TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
            ),
            LoweringExitTarget {LoweringExitKind::Unreachable, 0}
        );
    }
    auto predicate = std::optional<LoweringPredicate>();
    if (destination.continues()) {
        if (!result.accepted || !result.rejected) {
            predicate = LoweringKnownBool {result.accepted};
        } else {
            predicate = LoweringDynamicBool {name_expression(selected)};
        }
    }
    result.tests.push_back(
        std::move(destination).complete<LoweringPredicate>(std::move(predicate))
    );
    return result;
}

auto PatternRealizer::match(
    PatternID pattern_id,
    const PatternSubject& subject,
    bool accepts_on_entry
) noexcept -> ContinuationTask<PatternSelection> {
    const auto& pattern = body.pattern(pattern_id);
    const auto paths = completion.pattern(pattern_id);
    auto destination = LoweringStmtBuilder();
    const auto finish = [&](std::optional<LoweringPredicate> predicate) noexcept {
        return test(std::move(destination).complete<LoweringPredicate>(std::move(predicate)));
    };
    auto selection = co_await pattern.value.visit(
        Overloaded {
            [&](const WildcardPattern&) noexcept -> ContinuationTask<PatternSelection> {
                co_return finish(LoweringKnownBool {true});
            },
            [&](const RangePattern& value) noexcept -> ContinuationTask<PatternSelection> {
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
                    co_return finish(std::nullopt);
                }
                auto last = (co_await read(value.end, true));
                if (!destination.continues()) {
                    co_return finish(std::nullopt);
                }
                if (accepts_on_entry) {
                    co_return finish(LoweringKnownBool {true});
                }
                auto condition = std::optional<TargetExpr>();
                if (first) {
                    condition = binary_expression(
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
                    condition = condition ? binary_expression(
                                                std::move(*condition),
                                                TargetBinaryOperator::LogicalAnd,
                                                std::move(upper)
                                            )
                                          : std::move(upper);
                }
                if (!condition) {
                    co_return finish(LoweringKnownBool {true});
                }
                co_return finish(LoweringDynamicBool {std::move(*condition)});
            },
            [&](const LiteralPattern& value) noexcept -> ContinuationTask<PatternSelection> {
                if (accepts_on_entry) {
                    co_return finish(LoweringKnownBool {true});
                }
                co_return finish(
                    LoweringDynamicBool {binary_expression(
                        subject_expression(subject),
                        TargetBinaryOperator::Equal,
                        constant_expression(context, value.constant)
                    )}
                );
            },
            [&](const TypeConstraintPattern& value) noexcept -> ContinuationTask<PatternSelection> {
                if (value.type != pattern.type) {
                    invariant_violation("concrete type-constraint pattern changed its type");
                }
                co_return finish(LoweringKnownBool {true});
            },
            [&](const BindingPattern& value) noexcept -> ContinuationTask<PatternSelection> {
                auto result = finish(LoweringKnownBool {true});
                result.bindings.emplace(value.binding, subject);
                co_return result;
            },
            [&](const OrPattern& value) noexcept -> ContinuationTask<PatternSelection> {
                auto choices = std::vector<PatternSelection>();
                for (auto index = 0uz; index < value.alternatives.size(); ++index) {
                    // An accepting disjunction guarantees its final alternative
                    // only after all earlier alternatives have rejected.
                    auto candidate = co_await match(
                        value.alternatives[index],
                        subject,
                        accepts_on_entry && index + 1uz == value.alternatives.size()
                    );
                    const auto rejected = candidate.rejected;
                    choices.push_back(std::move(candidate));
                    if (!rejected) {
                        break;
                    }
                }
                co_return alternatives(std::move(choices));
            },
            [&](const EnumCasePattern& value) noexcept -> ContinuationTask<PatternSelection> {
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
                    if (single_case(value.enum_case) || accepts_on_entry) {
                        co_return finish(LoweringKnownBool {true});
                    }
                    co_return finish(
                        LoweringDynamicBool {binary_expression(
                            subject_expression(subject),
                            TargetBinaryOperator::Equal,
                            enum_case_expression(context, value.enum_case, {})
                        )}
                    );
                }
                const auto projection = context.target().add_local(
                    names.fresh(TargetTemporaryNameKind::SuccessProjection)
                );
                projections.push_back(projection);
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
                auto predicate = LoweringPredicate(LoweringKnownBool {true});
                if (!single_case(value.enum_case) && !accepts_on_entry) {
                    predicate = LoweringDynamicBool {binary_expression(
                        name_expression(projection),
                        TargetBinaryOperator::NotEqual,
                        intrinsic_expression(TargetSymbol::StdNullptr)
                    )};
                }
                auto result = finish(std::move(predicate));
                result.source_locals.insert(projection);
                for (auto index = 0uz; index < value.payload.size() && result.accepted; ++index) {
                    result = sequence(
                        std::move(result),
                        co_await match(
                            value.payload[index],
                            {.root = projection,
                             .dereference_root = true,
                             .payload_index = static_cast<std::uint32_t>(index)},
                            accepts_on_entry
                        )
                    );
                }
                co_return result;
            }
        }
    );
    selection.accepted &= paths.accepted;
    selection.rejected &= paths.rejected && !accepts_on_entry;
    co_return selection;
}

PatternRealizer::PatternRealizer(
    ModuleLowering& context,
    TargetNameAllocator& names,
    const SemIRBody& body,
    std::span<const SemPatternBounds> bounds,
    PatternBoundRealizer bound
) noexcept
    : context(context),
      names(names),
      body(body),
      completion(
          CompletionPatterns {
              .read = [this](PatternID id) noexcept
                  -> std::variant<PatternValue, ElaboratedPatternValue> {
                  return this->body.pattern(id).value;
              },
              .single_case = [this](EnumCaseID id) noexcept { return single_case(id); },
          },
          bounds
      ),
      bound(std::move(bound)) {}
