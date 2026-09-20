module carven:semantic.evaluation.control.impl;

import :semantic.evaluation.display;
import :semantic.evaluation.executor;
import std;

auto SemanticExecutor::region(ExecutionFrame& frame, const SemanticRegion& source) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    if (auto checked = step(source.origin); !checked) {
        co_return std::unexpected(checked.error());
    }
    for (const auto& child : source.statements) {
        auto result = (co_await statement(frame, child));
        if (!result || result->flow != ExecutionFlow::Normal) {
            co_return result;
        }
    }
    co_return source.result
        ? (co_await expression(frame, *source.result))
        : ExecutionResult<ExecutionCompletion>(
              ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}}
          );
}

auto SemanticExecutor::statement(ExecutionFrame& frame, const SemanticStatement& source) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    context.trace(
        {.kind = ExecutionTraceKind::Statement,
         .origin = source.origin,
         .function = std::nullopt,
         .depth = calls.size()}
    );
    if (auto checked = step(source.origin); !checked) {
        co_return std::unexpected(checked.error());
    }
    co_return (co_await source.value.visit(
        [&](const auto& operation) noexcept -> ExecutionTask<ExecutionCompletion> {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, SemReturn>) {
                auto result = operation.value ? (co_await value(frame, *operation.value))
                                              : ExecutionResult<ExecutionValue>(ExecutionVoid {});
                if (!result) {
                    co_return std::unexpected(result.error());
                }
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Return,
                    .value = std::move(*result)
                };
            } else if constexpr (std::same_as<Operation, SemThrow>) {
                auto payload = (co_await value(frame, operation.value));
                if (!payload) {
                    co_return std::unexpected(payload.error());
                }
                auto owned = own_storage(std::move(*payload), source.origin);
                if (!owned) {
                    co_return std::unexpected(owned.error());
                }
                co_return std::unexpected(
                    ExecutionFailure {ExecutionSourceFailure {
                        .type = operation.failure_type,
                        .payload = std::make_shared<const ExecutionValue>(std::move(*owned)),
                        .origin = source.origin,
                        .calls = calls,
                    }}
                );
            } else if constexpr (std::same_as<Operation, SemRethrow>) {
                if (frame.caught.empty()) {
                    co_return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "rethrow has no active caught failure"
                    ));
                }
                co_return std::unexpected(ExecutionFailure {frame.caught.back()});
            } else if constexpr (std::same_as<Operation, SemBreak>) {
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Break,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemContinue>) {
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Continue,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemExpressionStatement>) {
                auto result = (co_await expression(frame, operation.expression));
                if (result && result->flow == ExecutionFlow::Normal) {
                    result->value = ExecutionVoid {};
                }
                co_return result;
            } else if constexpr (std::same_as<Operation, SemInitialize>) {
                auto result = (co_await value(frame, operation.initializer));
                if (!result) {
                    co_return std::unexpected(result.error());
                }
                auto stored = own_storage(std::move(*result), source.origin);
                if (!stored) {
                    co_return std::unexpected(stored.error());
                }
                frame.slots[operation.binding.index()] = std::move(*stored);
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemAssign>) {
                auto target = (co_await place(frame, operation.target));
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                // Compound assignment snapshots its left operand before the right executes.
                auto prior = std::optional<ConstantFact>();
                if (operation.compound) {
                    auto selected = located(frame, *target, source.origin);
                    if (!selected) {
                        co_return std::unexpected(selected.error());
                    }
                    auto snapshot = read_fact(**selected, source.origin);
                    if (!snapshot) {
                        co_return std::unexpected(snapshot.error());
                    }
                    prior = std::move(*snapshot);
                }
                auto right = (co_await value(frame, operation.value));
                if (!right) {
                    co_return std::unexpected(right.error());
                }
                if (operation.compound) {
                    auto rhs = read_fact(*right, source.origin);
                    auto result_type = type(operation.target.type.construction(), source.origin);
                    if (!rhs || !result_type) {
                        co_return std::unexpected(!rhs ? rhs.error() : result_type.error());
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
                        co_return std::unexpected(right.error());
                    }
                }
                right = own_storage(std::move(*right), source.origin);
                if (!right) {
                    co_return std::unexpected(right.error());
                }
                if (target->path.empty()) {
                    frame.slots[target->slot] = std::move(*right);
                } else {
                    auto selected = located(frame, *target, source.origin);
                    if (!selected) {
                        co_return std::unexpected(selected.error());
                    }
                    **selected = std::move(*right);
                }
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemLoop>) {
                co_return (co_await loop(frame, operation, source.origin));
            } else if constexpr (std::same_as<Operation, SemRangeLoop>) {
                co_return (co_await range_loop(frame, operation, source.origin));
            } else if constexpr (std::same_as<Operation, OwnedSemanticRegion>) {
                co_return (co_await region(frame, *operation));
            } else {
                co_return std::unexpected(fail(
                    source.origin,
                    DiagnosticCode::ConstEvaluation,
                    "statement is not supported in execution"
                ));
            }
        }
    ));
}

auto SemanticExecutor::loop(
    ExecutionFrame& frame,
    const SemLoop& source,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionCompletion> {
    auto initialized = (co_await region(frame, *source.initializer));
    if (!initialized || initialized->flow != ExecutionFlow::Normal) {
        co_return initialized;
    }
    while (true) {
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(checked.error());
        }
        if (source.condition) {
            auto condition = (co_await value(frame, *source.condition));
            if (!condition) {
                co_return std::unexpected(condition.error());
            }
            auto truth = boolean(*condition, source.condition->origin);
            if (!truth) {
                co_return std::unexpected(truth.error());
            }
            if (!*truth) {
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            }
        }
        auto body = co_await region(frame, *source.body);
        if (!body || body->flow == ExecutionFlow::Return) {
            co_return body;
        }
        if (body->flow == ExecutionFlow::Break) {
            co_return ExecutionCompletion {
                .flow = ExecutionFlow::Normal,
                .value = ExecutionVoid {}
            };
        }
        auto steps = (co_await region(frame, *source.steps));
        if (!steps || steps->flow == ExecutionFlow::Return) {
            co_return steps;
        }
    }
}

auto SemanticExecutor::range_loop(
    ExecutionFrame& frame,
    const SemRangeLoop& source,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionCompletion> {
    const auto sequence_type = type(source.source.type.construction(), origin);
    if (!sequence_type) {
        co_return std::unexpected(sequence_type.error());
    }
    const auto canonical = values.type_copy(*sequence_type);
    if (const auto* range_type = std::get_if<RangeTypeValue>(&canonical.value)) {
        auto evaluated = (co_await value(frame, source.source));
        if (!evaluated) {
            co_return std::unexpected(evaluated.error());
        }
        auto fact = read_fact(*evaluated, origin);
        if (!fact) {
            co_return std::unexpected(fact.error());
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
                co_return std::unexpected(checked.error());
            }
            if (source.binding) {
                frame.slots[source.binding->index()] =
                    ConstantAtom {.type = range_type->element, .value = current};
            }
            auto body = co_await region(frame, *source.body);
            if (!body || body->flow == ExecutionFlow::Return) {
                co_return body;
            }
            if (body->flow == ExecutionFlow::Break || current == range.end) {
                break;
            }
            current = current.negative()
                ? IntegerConstant::from_parts(current.magnitude() - 1u, true)
                : IntegerConstant::from_parts(current.magnitude() + 1u, false);
        }
        co_return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
    }
    const auto slots = frame.slots.size();
    const auto execute = [&]() noexcept -> ExecutionTask<ExecutionCompletion> {
        auto owner = ExecutionPlace {.slot = slots, .path = {}};
        if (local_place(source.source)) {
            auto selected = (co_await place(frame, source.source));
            if (!selected) {
                co_return std::unexpected(selected.error());
            }
            owner = std::move(*selected);
        } else {
            auto evaluated = (co_await value(frame, source.source));
            if (!evaluated) {
                co_return std::unexpected(evaluated.error());
            }
            auto stored = own_storage(std::move(*evaluated), origin);
            if (!stored) {
                co_return std::unexpected(stored.error());
            }
            frame.slots.emplace_back(std::move(*stored));
        }
        auto storage = located(frame, owner, origin);
        if (!storage) {
            co_return std::unexpected(storage.error());
        }
        const auto view = execution_compound_view(values, **storage);
        if (!view) {
            co_return std::unexpected(
                fail(origin, DiagnosticCode::ConstEvaluation, "iteration requires array storage")
            );
        }
        const auto extent = view->size();
        auto borrow_element = source.access == AccessMode::Write;
        if (source.binding) {
            const auto element_type = type(frame.body->binding_type(*source.binding), origin);
            if (!element_type) {
                co_return std::unexpected(element_type.error());
            }
            borrow_element |= read_borrows_storage(*element_type);
        }
        for (auto index = 0uz; index < extent; ++index) {
            if (auto checked = step(origin); !checked) {
                co_return std::unexpected(checked.error());
            }
            if (source.binding) {
                auto element = owner;
                element.path.push_back(index);
                if (borrow_element) {
                    frame.slots[source.binding->index()] = std::move(element);
                } else {
                    auto selected = located(frame, element, origin);
                    if (!selected) {
                        co_return std::unexpected(selected.error());
                    }
                    auto copied = copy_value(**selected, origin);
                    if (!copied) {
                        co_return std::unexpected(copied.error());
                    }
                    frame.slots[source.binding->index()] = std::move(*copied);
                }
            }
            auto body = co_await region(frame, *source.body);
            if (!body || body->flow == ExecutionFlow::Return) {
                co_return body;
            }
            if (body->flow == ExecutionFlow::Break) {
                break;
            }
        }
        co_return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
    };
    auto result = (co_await execute());
    if (source.binding) {
        frame.slots[source.binding->index()] = ExecutionUninitialized {};
    }
    frame.slots.resize(slots);
    co_return result;
}

auto SemanticExecutor::report(
    ExecutionFrame& frame,
    const SemReport& operation,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    if (!testing && operation.kind != ReportKind::Assert) {
        co_return std::unexpected(
            fail(origin, DiagnosticCode::ConstTest, "test operation requires an active test")
        );
    }
    auto passed = false;
    auto explanation = std::string();
    if (operation.condition) {
        const auto previous = condition_observation;
        if (operation.operand_sources) {
            condition_observation = ConditionObservation {
                .condition = std::addressof(**operation.condition),
                .sources = *operation.operand_sources,
                .explanation = &explanation
            };
        }
        auto condition = (co_await value(frame, **operation.condition));
        condition_observation = previous;
        if (!condition) {
            co_return std::unexpected(condition.error());
        }
        auto truth = boolean(*condition, origin);
        if (!truth) {
            co_return std::unexpected(truth.error());
        }
        passed = *truth;
    }
    auto message = std::string();
    if (operation.message && !passed) {
        auto argument = (co_await value(frame, **operation.message));
        if (!argument) {
            co_return std::unexpected(argument.error());
        }
        auto bytes = text(*argument, origin);
        if (!bytes) {
            co_return std::unexpected(bytes.error());
        }
        message = *bytes;
    }
    if (!passed) {
        test_failed = true;
        auto detail = std::string(
            operation.kind == ReportKind::Assert        ? "assertion failed"
                : operation.kind == ReportKind::Check   ? "check failed"
                : operation.kind == ReportKind::Require ? "requirement failed"
                                                        : "explicit failure"
        );
        const auto field = [&](std::string_view label, std::string_view text) noexcept {
            detail += "\n  ";
            detail += label;
            if (text.empty()) {
                detail += " \"\"";
            } else if (text.find('\n') == std::string_view::npos) {
                detail += ' ';
                detail += text;
            } else {
                while (!text.empty()) {
                    detail += "\n    ";
                    const auto newline = text.find('\n');
                    detail += text.substr(0, newline);
                    if (newline == std::string_view::npos) {
                        break;
                    }
                    text.remove_prefix(newline + 1);
                }
            }
        };
        if (operation.condition_source) {
            field("condition:", values.spelling(*operation.condition_source));
        }
        if (!explanation.empty()) {
            field("operands:", explanation);
        }
        if (operation.message) {
            field("message:", message);
        }
        if (auto checked = account_text(detail.size(), origin); !checked) {
            co_return std::unexpected(checked.error());
        }
        const auto failure = fail(
            origin,
            operation.kind == ReportKind::Assert ? DiagnosticCode::AssertionFailed
                                                 : DiagnosticCode::ConstTest,
            std::move(detail),
            operation.kind
        );
        if (operation.kind != ReportKind::Check) {
            co_return std::unexpected(failure);
        }
    }
    co_return ExecutionVoid {};
}

auto SemanticExecutor::observe_condition(
    const SemanticExpression& source,
    const ExecutionValue& left,
    const ExecutionValue* right,
    bool passed
) noexcept -> void {
    if (passed || !condition_observation || condition_observation->condition != &source) {
        return;
    }
    auto& output = *condition_observation->explanation;
    output += std::string(values.spelling(condition_observation->sources[0])) + ": "
        + display_execution_value(values, left, true).value_or("<opaque>") + "\n";
    output += std::string(values.spelling(condition_observation->sources[1])) + ": "
        + (right ? display_execution_value(values, *right, true).value_or("<opaque>")
                 : "<not evaluated>")
        + "\n";
    if (output.size() > 16384uz) {
        auto end = 16384uz;
        while (end != 0 && (static_cast<unsigned char>(output[end]) & 0xc0u) == 0x80u) {
            --end;
        }
        output.resize(end);
        output += "...";
    }
}
