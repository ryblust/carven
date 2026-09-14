module carven:semantic.evaluation.control.impl;

import :semantic.evaluation.executor;
import std;

auto ConstantExecutor::region(ConstantFrame& frame, const SemanticRegion& source) noexcept
    -> ConstantExecutionResult<ConstantCompletion> {
    if (auto checked = step(source.origin); !checked) {
        return std::unexpected(checked.error());
    }
    for (const auto& child : source.statements) {
        auto result = statement(frame, child);
        if (!result || result->flow != ConstantFlow::Normal) {
            return result;
        }
    }
    return source.result ? expression(frame, *source.result)
                         : ConstantExecutionResult<ConstantCompletion>(ConstantCompletion {});
}

auto ConstantExecutor::statement(ConstantFrame& frame, const SemanticStatement& source) noexcept
    -> ConstantExecutionResult<ConstantCompletion> {
    if (auto checked = step(source.origin); !checked) {
        return std::unexpected(checked.error());
    }
    return std::visit(
        [&](const auto& operation) noexcept -> ConstantExecutionResult<ConstantCompletion> {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, SemReturn>) {
                auto result = operation.value
                    ? value(frame, *operation.value)
                    : ConstantExecutionResult<ConstantExecutionValue>(ConstantVoid {});
                if (!result) {
                    return std::unexpected(result.error());
                }
                return ConstantCompletion {
                    .flow = ConstantFlow::Return,
                    .value = std::move(*result)
                };
            } else if constexpr (std::same_as<Operation, SemBreak>) {
                return ConstantCompletion {.flow = ConstantFlow::Break, .value = ConstantVoid {}};
            } else if constexpr (std::same_as<Operation, SemContinue>) {
                return ConstantCompletion {
                    .flow = ConstantFlow::Continue,
                    .value = ConstantVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemExpressionStatement>) {
                auto result = expression(frame, operation.expression);
                if (result && result->flow == ConstantFlow::Normal) {
                    result->value = ConstantVoid {};
                }
                return result;
            } else if constexpr (std::same_as<Operation, SemInitialize>) {
                auto result = value(frame, operation.initializer);
                if (!result) {
                    return std::unexpected(result.error());
                }
                auto stored = own_storage(std::move(*result), source.origin);
                if (!stored) {
                    return std::unexpected(stored.error());
                }
                frame.slots[operation.binding.index()] = std::move(*stored);
                return ConstantCompletion {};
            } else if constexpr (std::same_as<Operation, SemAssign>) {
                auto target = place(frame, operation.target);
                if (!target) {
                    return std::unexpected(target.error());
                }
                // Compound assignment snapshots its left operand before the right executes.
                auto prior = std::optional<ConstantFact>();
                if (operation.compound) {
                    auto selected = located(frame, *target, source.origin);
                    if (!selected) {
                        return std::unexpected(selected.error());
                    }
                    auto snapshot = read_fact(**selected, source.origin);
                    if (!snapshot) {
                        return std::unexpected(snapshot.error());
                    }
                    prior = std::move(*snapshot);
                }
                auto right = value(frame, operation.value);
                if (!right) {
                    return std::unexpected(right.error());
                }
                if (operation.compound) {
                    auto rhs = read_fact(*right, source.origin);
                    auto result_type = type(operation.target.type.construction(), source.origin);
                    if (!rhs || !result_type) {
                        return std::unexpected(!rhs ? rhs.error() : result_type.error());
                    }
                    right = finish(
                        evaluate_binary_constant_value(
                            values,
                            *operation.compound,
                            *prior,
                            *rhs,
                            *result_type
                        ),
                        source.origin
                    );
                    if (!right) {
                        return std::unexpected(right.error());
                    }
                }
                right = own_storage(std::move(*right), source.origin);
                if (!right) {
                    return std::unexpected(right.error());
                }
                if (target->path.empty()) {
                    frame.slots[target->slot] = std::move(*right);
                } else {
                    auto selected = located(frame, *target, source.origin);
                    if (!selected) {
                        return std::unexpected(selected.error());
                    }
                    **selected = std::move(*right);
                }
                return ConstantCompletion {};
            } else if constexpr (std::same_as<Operation, SemLoop>) {
                return loop(frame, operation, source.origin);
            } else if constexpr (std::same_as<Operation, SemRangeLoop>) {
                return range_loop(frame, operation, source.origin);
            } else if constexpr (std::same_as<Operation, OwnedSemanticRegion>) {
                return region(frame, *operation);
            } else {
                return std::unexpected(fail(
                    source.origin,
                    DiagnosticCode::ConstEvaluation,
                    "statement is not supported in const execution"
                ));
            }
        },
        source.value
    );
}

auto ConstantExecutor::loop(
    ConstantFrame& frame,
    const SemLoop& source,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantCompletion> {
    auto initialized = region(frame, *source.initializer);
    if (!initialized || initialized->flow != ConstantFlow::Normal) {
        return initialized;
    }
    while (true) {
        if (auto checked = step(origin); !checked) {
            return std::unexpected(checked.error());
        }
        if (source.condition) {
            auto condition = value(frame, *source.condition);
            if (!condition) {
                return std::unexpected(condition.error());
            }
            auto truth = boolean(*condition, source.condition->origin);
            if (!truth) {
                return std::unexpected(truth.error());
            }
            if (!*truth) {
                return ConstantCompletion {};
            }
        }
        auto body = region(frame, *source.body);
        if (!body || body->flow == ConstantFlow::Return) {
            return body;
        }
        if (body->flow == ConstantFlow::Break) {
            return ConstantCompletion {};
        }
        auto steps = region(frame, *source.steps);
        if (!steps || steps->flow == ConstantFlow::Return) {
            return steps;
        }
    }
}

auto ConstantExecutor::sequence_loop(
    ConstantFrame& frame,
    const SemRangeLoop& source,
    const SemSequenceRange& sequence,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantCompletion> {
    const auto slots = frame.slots.size();
    const auto execute = [&]() noexcept -> ConstantExecutionResult<ConstantCompletion> {
        auto owner = ConstantPlace {.slot = slots, .path = {}};
        if (local_place(sequence.value)) {
            auto selected = place(frame, sequence.value);
            if (!selected) {
                return std::unexpected(selected.error());
            }
            owner = std::move(*selected);
        } else {
            auto evaluated = value(frame, sequence.value);
            if (!evaluated) {
                return std::unexpected(evaluated.error());
            }
            auto stored = own_storage(std::move(*evaluated), origin);
            if (!stored) {
                return std::unexpected(stored.error());
            }
            frame.slots.emplace_back(std::move(*stored));
        }
        auto storage = located(frame, owner, origin);
        if (!storage) {
            return std::unexpected(storage.error());
        }
        const auto view = constant_compound_view(values, **storage);
        if (!view) {
            return std::unexpected(fail(
                origin,
                DiagnosticCode::ConstEvaluation,
                "constant iteration requires array storage"
            ));
        }
        const auto extent = view->size();
        auto borrow_element = source.access == AccessMode::Write;
        if (source.binding) {
            const auto element_type = type(frame.body->bindings.get(*source.binding).type, origin);
            if (!element_type) {
                return std::unexpected(element_type.error());
            }
            borrow_element |= read_borrows_storage(*element_type);
        }
        for (auto index = 0uz; index < extent; ++index) {
            if (auto checked = step(origin); !checked) {
                return std::unexpected(checked.error());
            }
            if (source.binding) {
                auto element = owner;
                element.path.push_back(index);
                if (borrow_element) {
                    frame.slots[source.binding->index()] = std::move(element);
                } else {
                    auto selected = located(frame, element, origin);
                    if (!selected) {
                        return std::unexpected(selected.error());
                    }
                    auto copied = copy_value(**selected, origin);
                    if (!copied) {
                        return std::unexpected(copied.error());
                    }
                    frame.slots[source.binding->index()] = std::move(*copied);
                }
            }
            auto body = region(frame, *source.body);
            if (!body || body->flow == ConstantFlow::Return) {
                return body;
            }
            if (body->flow == ConstantFlow::Break) {
                break;
            }
        }
        return ConstantCompletion {};
    };
    auto result = execute();
    if (source.binding) {
        frame.slots[source.binding->index()] = ConstantUninitialized {};
    }
    frame.slots.resize(slots);
    return result;
}

auto ConstantExecutor::range_loop(
    ConstantFrame& frame,
    const SemRangeLoop& source,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantCompletion> {
    const auto* range = std::get_if<SemIntegerRange>(&source.source);
    if (range == nullptr) {
        return sequence_loop(frame, source, std::get<SemSequenceRange>(source.source), origin);
    }
    auto begin = value(frame, range->begin);
    if (!begin) {
        return std::unexpected(begin.error());
    }
    auto end = value(frame, range->end);
    if (!end) {
        return std::unexpected(end.error());
    }
    auto current = read_fact(*begin, origin);
    auto bound = read_fact(*end, origin);
    if (!current || !bound) {
        return std::unexpected(!current ? current.error() : bound.error());
    }
    const auto one =
        ConstantFact {.type = current->type, .value = IntegerConstant::from_parts(1, false)};
    const auto boolean_type = values.intern_builtin_type(BuiltinType::Bool);
    while (true) {
        if (auto checked = step(origin); !checked) {
            return std::unexpected(checked.error());
        }
        auto comparison = finish(
            evaluate_binary_constant_value(
                values,
                BinaryOperator::Less,
                *current,
                *bound,
                boolean_type
            ),
            origin
        );
        if (!comparison) {
            return std::unexpected(comparison.error());
        }
        auto truth = boolean(*comparison, origin);
        if (!truth) {
            return std::unexpected(truth.error());
        }
        if (!*truth) {
            return ConstantCompletion {};
        }
        if (source.binding) {
            frame.slots[source.binding->index()] = ConstantAtom {
                .type = current->type,
                .value = *std::get_if<IntegerConstant>(&current->value)
            };
        }
        auto body = region(frame, *source.body);
        if (!body || body->flow == ConstantFlow::Return) {
            return body;
        }
        if (body->flow == ConstantFlow::Break) {
            return ConstantCompletion {};
        }
        auto incremented = finish(
            evaluate_binary_constant_value(
                values,
                BinaryOperator::Add,
                *current,
                one,
                current->type
            ),
            origin
        );
        if (!incremented) {
            return std::unexpected(incremented.error());
        }
        current = read_fact(*incremented, origin);
        if (!current) {
            return std::unexpected(current.error());
        }
    }
}

auto ConstantExecutor::test_report(
    ConstantFrame& frame,
    const SemTestReport& operation,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    if (!testing) {
        return std::unexpected(
            fail(origin, DiagnosticCode::ConstTest, "test operation requires an active const test")
        );
    }
    auto passed = false;
    if (operation.condition) {
        auto condition = value(frame, **operation.condition);
        if (!condition) {
            return std::unexpected(condition.error());
        }
        auto truth = boolean(*condition, origin);
        if (!truth) {
            return std::unexpected(truth.error());
        }
        passed = *truth;
    }
    auto message = std::string();
    if (operation.message) {
        auto argument = value(frame, **operation.message);
        if (!argument) {
            return std::unexpected(argument.error());
        }
        auto bytes = text(*argument, origin);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        if (!passed) {
            message = *bytes;
        }
    }
    if (!passed) {
        test_failed = true;
        auto detail = operation.condition_source
            ? std::format(
                  "{} failed: {}",
                  operation.kind == TestReportKind::Require ? "require" : "check",
                  values.spelling(*operation.condition_source)
              )
            : std::string("test failed");
        if (!message.empty()) {
            detail += ": " + message;
        }
        if (auto checked = account_text(detail.size(), origin); !checked) {
            return std::unexpected(checked.error());
        }
        const auto failure = fail(origin, DiagnosticCode::ConstTest, std::move(detail));
        if (operation.kind != TestReportKind::Check) {
            return std::unexpected(failure);
        }
    }
    return ConstantVoid {};
}
