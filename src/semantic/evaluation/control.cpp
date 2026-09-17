module carven:semantic.evaluation.control.impl;

import :semantic.evaluation.executor;
import std;

auto SemanticExecutor::region(ExecutionFrame& frame, const SemanticRegion& source) noexcept
    -> ExecutionResult<ExecutionCompletion> {
    if (auto checked = step(source.origin); !checked) {
        return std::unexpected(checked.error());
    }
    for (const auto& child : source.statements) {
        auto result = statement(frame, child);
        if (!result || result->flow != ExecutionFlow::Normal) {
            return result;
        }
    }
    return source.result
        ? expression(frame, *source.result)
        : ExecutionResult<ExecutionCompletion>(
              ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}}
          );
}

auto SemanticExecutor::statement(ExecutionFrame& frame, const SemanticStatement& source) noexcept
    -> ExecutionResult<ExecutionCompletion> {
    context.trace(
        {.kind = ExecutionTraceKind::Statement,
         .origin = source.origin,
         .function = std::nullopt,
         .depth = calls.size()}
    );
    if (auto checked = step(source.origin); !checked) {
        return std::unexpected(checked.error());
    }
    return source.value.visit(
        [&](const auto& operation) noexcept -> ExecutionResult<ExecutionCompletion> {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, SemReturn>) {
                auto result = operation.value ? value(frame, *operation.value)
                                              : ExecutionResult<ExecutionValue>(ExecutionVoid {});
                if (!result) {
                    return std::unexpected(result.error());
                }
                return ExecutionCompletion {
                    .flow = ExecutionFlow::Return,
                    .value = std::move(*result)
                };
            } else if constexpr (std::same_as<Operation, SemThrow>) {
                auto payload = value(frame, operation.value);
                if (!payload) {
                    return std::unexpected(payload.error());
                }
                auto owned = own_storage(std::move(*payload), source.origin);
                if (!owned) {
                    return std::unexpected(owned.error());
                }
                return std::unexpected(
                    ExecutionFailure {ExecutionSourceFailure {
                        .type = operation.failure_type,
                        .payload = std::make_shared<const ExecutionValue>(std::move(*owned)),
                        .origin = source.origin,
                        .calls = calls,
                    }}
                );
            } else if constexpr (std::same_as<Operation, SemRethrow>) {
                if (frame.caught.empty()) {
                    return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "rethrow has no active caught failure"
                    ));
                }
                return std::unexpected(ExecutionFailure {frame.caught.back()});
            } else if constexpr (std::same_as<Operation, SemBreak>) {
                return ExecutionCompletion {
                    .flow = ExecutionFlow::Break,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemContinue>) {
                return ExecutionCompletion {
                    .flow = ExecutionFlow::Continue,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemExpressionStatement>) {
                auto result = expression(frame, operation.expression);
                if (result && result->flow == ExecutionFlow::Normal) {
                    result->value = ExecutionVoid {};
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
                return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
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
                            *result_type,
                            context.arithmetic()
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
                return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
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
                    "statement is not supported in execution"
                ));
            }
        }
    );
}

auto SemanticExecutor::loop(
    ExecutionFrame& frame,
    const SemLoop& source,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionCompletion> {
    auto initialized = region(frame, *source.initializer);
    if (!initialized || initialized->flow != ExecutionFlow::Normal) {
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
                return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            }
        }
        auto body = region(frame, *source.body);
        if (!body || body->flow == ExecutionFlow::Return) {
            return body;
        }
        if (body->flow == ExecutionFlow::Break) {
            return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
        }
        auto steps = region(frame, *source.steps);
        if (!steps || steps->flow == ExecutionFlow::Return) {
            return steps;
        }
    }
}

auto SemanticExecutor::range_loop(
    ExecutionFrame& frame,
    const SemRangeLoop& source,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionCompletion> {
    const auto sequence_type = type(source.source.type.construction(), origin);
    if (!sequence_type) {
        return std::unexpected(sequence_type.error());
    }
    const auto canonical = values.type_copy(*sequence_type);
    if (const auto* range_type = std::get_if<RangeTypeValue>(&canonical.value)) {
        auto evaluated = value(frame, source.source);
        if (!evaluated) {
            return std::unexpected(evaluated.error());
        }
        auto fact = read_fact(*evaluated, origin);
        if (!fact) {
            return std::unexpected(fact.error());
        }
        const auto range = std::get<RangeConstant>(fact->value);
        auto current = range.begin;
        const auto less = [](IntegerConstant a, IntegerConstant b) static noexcept {
            return a.negative() != b.negative() ? a.negative()
                : a.negative()                  ? a.magnitude() > b.magnitude()
                                                : a.magnitude() < b.magnitude();
        };
        while (less(current, range.end) || (range.inclusive && current == range.end)) {
            if (auto checked = step(origin); !checked) {
                return std::unexpected(checked.error());
            }
            if (source.binding) {
                frame.slots[source.binding->index()] =
                    ConstantAtom {.type = range_type->element, .value = current};
            }
            auto body = region(frame, *source.body);
            if (!body || body->flow == ExecutionFlow::Return) {
                return body;
            }
            if (body->flow == ExecutionFlow::Break || current == range.end) {
                break;
            }
            current = current.negative()
                ? IntegerConstant::from_parts(current.magnitude() - 1u, true)
                : IntegerConstant::from_parts(current.magnitude() + 1u, false);
        }
        return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
    }
    const auto slots = frame.slots.size();
    const auto execute = [&]() noexcept -> ExecutionResult<ExecutionCompletion> {
        auto owner = ExecutionPlace {.slot = slots, .path = {}};
        if (local_place(source.source)) {
            auto selected = place(frame, source.source);
            if (!selected) {
                return std::unexpected(selected.error());
            }
            owner = std::move(*selected);
        } else {
            auto evaluated = value(frame, source.source);
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
        const auto view = execution_compound_view(values, **storage);
        if (!view) {
            return std::unexpected(
                fail(origin, DiagnosticCode::ConstEvaluation, "iteration requires array storage")
            );
        }
        const auto extent = view->size();
        auto borrow_element = source.access == AccessMode::Write;
        if (source.binding) {
            const auto element_type = type(frame.body->binding_type(*source.binding), origin);
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
            if (!body || body->flow == ExecutionFlow::Return) {
                return body;
            }
            if (body->flow == ExecutionFlow::Break) {
                break;
            }
        }
        return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
    };
    auto result = execute();
    if (source.binding) {
        frame.slots[source.binding->index()] = ExecutionUninitialized {};
    }
    frame.slots.resize(slots);
    return result;
}

auto SemanticExecutor::test_report(
    ExecutionFrame& frame,
    const SemTestReport& operation,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
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
    return ExecutionVoid {};
}
