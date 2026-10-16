module carven:semantic.evaluation.control.impl;

import :semantic.evaluation.display;
import :semantic.evaluation.executor;
import :semantic.evaluation.operation;
import :semantic.semir.stage;
import :support.invariant;
import :support.utf8;
import std;

auto SemanticExecutor::region(
    ExecutionFrame& frame,
    const SemanticRegion& source,
    bool cleanup
) noexcept -> ExecutionTask<ExecutionCompletion> {
    const auto finish = [&](
                            ExecutionResult<ExecutionCompletion> result
                        ) noexcept -> ExecutionTask<ExecutionCompletion> {
        if (cleanup) {
            co_return co_await close_region(frame, source.lifetime, std::move(result));
        }
        co_return result;
    };
    if (auto checked = step(source.origin); !checked) {
        co_return co_await finish(std::unexpected(std::move(checked.error())));
    }
    for (const auto& child : source.statements) {
        auto result = co_await statement(frame, child);
        if (!result || result->flow != ExecutionFlow::Normal) {
            co_return co_await finish(std::move(result));
        }
    }
    co_return co_await finish(
        source.result
            ? (co_await expression(frame, *source.result))
            : ExecutionResult<ExecutionCompletion>(
                  ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}}
              )
    );
}

auto SemanticExecutor::statement(ExecutionFrame& frame, const SemanticStatement& source) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    context.trace(
        {.kind = ExecutionTraceKind::Statement,
         .origin = source.origin,
         .function = std::nullopt,
         .depth = current->calls.size()}
    );
    if (auto checked = step(source.origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    const auto temporaries = frame.temporaries.size();
    auto result = (co_await source.value.visit(
        [&](const auto& operation) noexcept -> ExecutionTask<ExecutionCompletion> {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, SemReturn>) {
                auto result = operation.value ? (co_await this->value(frame, *operation.value))
                                              : ExecutionResult<ExecutionValue>(ExecutionVoid {});
                if (!result) {
                    co_return std::unexpected(std::move(result.error()));
                }
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Return,
                    .value = std::move(*result)
                };
            } else if constexpr (std::same_as<Operation, SemThrow>) {
                auto payload = (co_await this->value(frame, operation.value));
                if (!payload) {
                    co_return std::unexpected(std::move(payload.error()));
                }
                auto owned = own_storage(std::move(*payload), source.origin);
                if (!owned) {
                    co_return std::unexpected(std::move(owned.error()));
                }
                co_return std::unexpected(
                    ExecutionFailure {ExecutionSourceFailure {
                        .type = operation.failure_type,
                        .payload = std::make_shared<const ExecutionValue>(std::move(*owned)),
                        .origin = source.origin,
                        .calls = current->calls,
                        .blocks = current->blocks,
                    }}
                );
            } else if constexpr (std::same_as<Operation, SemRethrow>) {
                if (frame.caught.empty()) {
                    co_return std::unexpected(fail(
                        source.origin,
                        ExecutionReason::Evaluation,
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
            } else if constexpr (std::same_as<Operation, SemAsyncLet>) {
                co_return co_await start_child(frame, operation, source.origin);
            } else if constexpr (std::same_as<Operation, SemDecompose>) {
                if (operation.initializer) {
                    auto evaluated = co_await this->value(frame, *operation.initializer);
                    if (!evaluated) {
                        co_return std::unexpected(std::move(evaluated.error()));
                    }
                    auto product = own_storage(std::move(*evaluated), source.origin);
                    if (!product) {
                        co_return std::unexpected(std::move(product.error()));
                    }
                    bind(frame, operation.owner.index(), std::move(*product));
                }
                const auto owner =
                    std::get<ExecutionOwner>(frame.slots[operation.owner.index()]).place;
                for (const auto& binding : operation.bindings) {
                    auto element = owner;
                    for (const auto index : binding.path) {
                        auto child = memory.project(std::move(element), index);
                        if (!child) {
                            co_return std::unexpected(fail(
                                source.origin,
                                ExecutionReason::Evaluation,
                                "tuple decomposition has an invalid product path"
                            ));
                        }
                        element = std::move(*child);
                    }
                    release(frame, binding.binding.index());
                    frame.slots[binding.binding.index()] =
                        ExecutionOwner {.place = memory.adopt_projection(std::move(element))};
                }
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemInitialize>) {
                auto result = (co_await this->value(frame, operation.initializer));
                if (!result) {
                    co_return std::unexpected(std::move(result.error()));
                }
                auto stored = own_storage(std::move(*result), source.origin);
                if (!stored) {
                    co_return std::unexpected(std::move(stored.error()));
                }
                bind(frame, operation.binding.index(), std::move(*stored));
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemStaticBinding>) {
                // A constant is a static root: no handler receives its failure.
                auto result = (co_await this->value(frame, *operation.initializer));
                if (!result) {
                    co_return std::unexpected(escaped(std::move(result.error())));
                }
                auto constant = freeze(std::move(*result), source.origin);
                if (!constant) {
                    co_return std::unexpected(std::move(constant.error()));
                }
                auto binding_type =
                    type(frame.body->binding_type(operation.binding), source.origin);
                if (!binding_type) {
                    co_return std::unexpected(std::move(binding_type.error()));
                }
                if (values.constant(*constant).type != *binding_type) {
                    co_return std::unexpected(fail(
                        source.origin,
                        ExecutionReason::Admission,
                        "constant has no frozen representation of its declared type"
                    ));
                }
                auto stored = own_storage(ExecutionValue(*constant), source.origin);
                if (!stored) {
                    co_return std::unexpected(std::move(stored.error()));
                }
                bind(frame, operation.binding.index(), std::move(*stored));
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemAssign>) {
                const auto* binding = std::get_if<SemBinding>(&operation.target.value);
                const auto restore = binding != nullptr
                    && std::holds_alternative<ExecutionTaken>(
                                         frame.slots[binding->binding.index()]
                    );
                auto target = std::optional<ExecutionPlace>();
                if (!restore) {
                    auto selected = (co_await place(frame, operation.target));
                    if (!selected) {
                        co_return std::unexpected(std::move(selected.error()));
                    }
                    target = std::move(*selected);
                }
                // Compound assignment snapshots its left operand before the right executes.
                auto prior = std::optional<ConstantFact>();
                if (operation.compound) {
                    auto selected = located(*target, source.origin);
                    if (!selected) {
                        co_return std::unexpected(std::move(selected.error()));
                    }
                    auto snapshot = read_fact(**selected, source.origin);
                    if (!snapshot) {
                        co_return std::unexpected(std::move(snapshot.error()));
                    }
                    prior = std::move(*snapshot);
                }
                auto right = (co_await this->value(frame, operation.value));
                if (!right) {
                    co_return std::unexpected(std::move(right.error()));
                }
                if (operation.compound) {
                    auto rhs = read_fact(*right, source.origin);
                    auto result_type = type(operation.target.type.construction(), source.origin);
                    if (!rhs || !result_type) {
                        co_return std::unexpected(
                            !rhs ? std::move(rhs.error()) : std::move(result_type.error())
                        );
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
                        co_return std::unexpected(std::move(right.error()));
                    }
                }
                right = own_storage(std::move(*right), source.origin);
                if (!right) {
                    co_return std::unexpected(std::move(right.error()));
                }
                if (binding != nullptr
                    && std::holds_alternative<ExecutionTaken>(
                        frame.slots[binding->binding.index()]
                    )) {
                    bind(frame, binding->binding.index(), std::move(*right));
                } else if (!target || !memory.assign(*target, std::move(*right))) {
                    co_return std::unexpected(fail(
                        source.origin,
                        ExecutionReason::Evaluation,
                        "pointer target is no longer alive"
                    ));
                }
                if (target) {
                    const auto* root = memory.owning_value(*target);
                    if (root) {
                        if (auto checked = check_value_size(*root, source.origin); !checked) {
                            co_return std::unexpected(std::move(checked.error()));
                        }
                    }
                }
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemLoop>) {
                auto result = (co_await loop(frame, operation, source.origin));
                co_return co_await close_region(
                    frame,
                    operation.initializer->lifetime,
                    std::move(result)
                );
            } else if constexpr (std::same_as<Operation, SemRangeLoop>) {
                auto result = (co_await range_loop(frame, operation, source.origin));
                co_return co_await close_region(frame, operation.lifetime, std::move(result));
            } else if constexpr (std::same_as<Operation, SemExpandedLoop>) {
                for (const auto& iteration : operation.iterations) {
                    auto result = co_await region(frame, iteration);
                    if (!result || result->flow == ExecutionFlow::Return) {
                        co_return result;
                    }
                    if (result->flow == ExecutionFlow::Break) {
                        break;
                    }
                }
                co_return ExecutionCompletion {
                    .flow = ExecutionFlow::Normal,
                    .value = ExecutionVoid {}
                };
            } else if constexpr (std::same_as<Operation, SemConstBlock>) {
                current->blocks.push_back({.label = operation.label, .origin = operation.source});
                auto result = co_await region(frame, *operation.region);
                if (!result) {
                    result = std::unexpected(escaped(std::move(result.error())));
                }
                current->blocks.pop_back();
                co_return result;
            } else if constexpr (std::same_as<Operation, OwnedSemanticRegion>) {
                co_return (co_await region(frame, *operation));
            } else {
                static_assert(std::same_as<Operation, void>, "statement execution is missing");
            }
        }
    ));
    release_temporaries(frame, temporaries);
    co_return result;
}

auto SemanticExecutor::loop(
    ExecutionFrame& frame,
    const SemLoop& source,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionCompletion> {
    auto initialized = (co_await region(frame, *source.initializer, false));
    if (!initialized || initialized->flow != ExecutionFlow::Normal) {
        co_return initialized;
    }
    while (true) {
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (source.condition) {
            const auto temporaries = frame.temporaries.size();
            auto condition = (co_await this->value(frame, *source.condition));
            release_temporaries(frame, temporaries);
            if (!condition) {
                co_return std::unexpected(std::move(condition.error()));
            }
            auto truth = boolean(*condition, source.condition->origin);
            if (!truth) {
                co_return std::unexpected(std::move(truth.error()));
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
        auto steps = (co_await region(frame, *source.steps, false));
        if (!steps || steps->flow == ExecutionFlow::Return) {
            co_return steps;
        }
    }
}

auto SemanticExecutor::open_iteration(
    ExecutionFrame& frame,
    const SemanticExpression& source
) noexcept -> ExecutionTask<IterationCursor> {
    const auto reference = execution_type_reference(values, source.type.construction());
    const auto* concrete = std::get_if<TypeID>(&reference);
    const auto canonical = concrete ? std::optional(values.type_copy(*concrete)) : std::nullopt;
    if (const auto* range = canonical ? std::get_if<RangeTypeValue>(&canonical->value) : nullptr) {
        auto evaluated = co_await value(frame, source);
        if (!evaluated) {
            co_return std::unexpected(std::move(evaluated.error()));
        }
        auto fact = read_fact(*evaluated, source.origin);
        if (!fact) {
            co_return std::unexpected(std::move(fact.error()));
        }
        co_return IterationCursor(
            IntegerIteration {
                .element = range->element,
                .cursor = IntegerRangeCursor(std::get<RangeConstant>(fact->value)),
            }
        );
    }
    if (canonical
        && canonical->value == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::StrCharsView}}) {
        auto evaluated = co_await value(frame, source);
        if (!evaluated) {
            co_return std::unexpected(std::move(evaluated.error()));
        }
        auto text = text_handle(*evaluated, source.origin);
        if (!text) {
            co_return std::unexpected(std::move(text.error()));
        }
        co_return IterationCursor(CharacterIteration {.text = std::move(*text), .offset = 0uz});
    }
    auto sequence = co_await sequence_view(frame, source, source.origin);
    if (!sequence) {
        co_return std::unexpected(std::move(sequence.error()));
    }
    co_return IterationCursor(SequenceIteration {.slice = std::move(*sequence), .index = 0uz});
}

auto SemanticExecutor::next_iteration(IterationCursor& cursor, ProgramOriginID origin) noexcept
    -> ExecutionResult<std::optional<ExecutionOperand>> {
    return cursor.visit(
        [&](auto& current) noexcept -> ExecutionResult<std::optional<ExecutionOperand>> {
            using Cursor = std::remove_cvref_t<decltype(current)>;
            if constexpr (std::same_as<Cursor, IntegerIteration>) {
                const auto next = current.cursor.next();
                if (!next) {
                    return std::nullopt;
                }
                if (auto checked = step(origin); !checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                return std::optional(
                    ExecutionOperand {ExecutionValue(
                        ConstantAtom {
                            .type = current.element,
                            .value = *next,
                        }
                    )}
                );
            } else if constexpr (std::same_as<Cursor, CharacterIteration>) {
                const auto bytes = current.text.observe(values, &memory, current.offset, 4uz);
                if (!bytes) {
                    return std::unexpected(
                        fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
                    );
                }
                if (bytes->size() == 0uz) {
                    return std::nullopt;
                }
                if (auto checked = step(origin); !checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                const auto decoded = UTF8Decoder::decode(bytes->view(), 0uz);
                if (!decoded.valid) {
                    return std::unexpected(fail(
                        origin,
                        ExecutionReason::Evaluation,
                        "character iteration requires valid UTF-8"
                    ));
                }
                current.offset += decoded.width;
                return std::optional(
                    ExecutionOperand {ExecutionValue(
                        ConstantAtom {
                            .type = values.builtin_type(BuiltinType::Char),
                            .value = CharacterConstant {.scalar = decoded.scalar},
                        }
                    )}
                );
            } else {
                if (current.index == current.slice.extent) {
                    return std::nullopt;
                }
                if (auto checked = step(origin); !checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                auto selected = slice_element(current.slice, current.index++, origin);
                if (!selected) {
                    return std::unexpected(std::move(selected.error()));
                }
                return std::optional(ExecutionOperand {std::move(*selected)});
            }
        }
    );
}

auto SemanticExecutor::range_loop(
    ExecutionFrame& frame,
    const SemRangeLoop& source,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionCompletion> {
    auto cursor = co_await open_iteration(frame, source.source);
    if (!cursor) {
        co_return std::unexpected(std::move(cursor.error()));
    }
    auto borrow_element = source.access == AccessMode::Write;
    if (source.binding) {
        borrow_element |= read_borrows_storage(frame.body->binding_type(*source.binding));
    }
    const auto execute = [&]() noexcept -> ExecutionTask<ExecutionCompletion> {
        while (true) {
            auto next = next_iteration(*cursor, origin);
            if (!next) {
                co_return std::unexpected(std::move(next.error()));
            }
            if (!*next) {
                break;
            }
            if (source.binding) {
                auto element = std::move(**next);
                if (borrow_element && std::holds_alternative<ExecutionPlace>(element)) {
                    release(frame, source.binding->index());
                    frame.slots[source.binding->index()] =
                        std::get<ExecutionPlace>(std::move(element));
                } else {
                    auto copied = materialize(std::move(element), origin);
                    if (!copied) {
                        co_return std::unexpected(std::move(copied.error()));
                    }
                    bind(frame, source.binding->index(), std::move(*copied));
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
    auto result = co_await execute();
    if (source.binding) {
        release(frame, source.binding->index());
    }
    co_return result;
}

auto SemanticExecutor::root_iteration(
    const SemanticExpression& source,
    const ExecutionIterationConsumer& consume,
    std::optional<ExecutionBody> body
) noexcept -> ExecutionTask<ExecutionValue> {
    auto frame = ExecutionFrame {
        .body = body,
        .slots = std::vector<ExecutionSlot>(body ? body->binding_count() : 0uz),
        .caught = {},
        .temporaries = {},
        .children = {},
    };
    const auto execute = [&]() noexcept -> ExecutionTask<ExecutionValue> {
        auto cursor = co_await open_iteration(frame, source);
        if (!cursor) {
            co_return std::unexpected(std::move(cursor.error()));
        }
        while (true) {
            auto next = next_iteration(*cursor, source.origin);
            if (!next) {
                co_return std::unexpected(std::move(next.error()));
            }
            if (!*next) {
                break;
            }
            auto continued = co_await consume(std::move(**next), *this);
            if (!continued) {
                co_return std::unexpected(std::move(continued.error()));
            }
            if (!*continued) {
                break;
            }
        }
        co_return ExecutionVoid {};
    };
    auto result = co_await execute();
    release_frame(frame);
    co_return result;
}

auto SemanticExecutor::report(
    ExecutionFrame& frame,
    const SemReport& operation,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    if (!testing && operation.kind != ReportKind::Assert) {
        co_return std::unexpected(
            trap(origin, ExecutionReason::Test, "test operation requires an active test")
        );
    }
    auto passed = false;
    auto explanation = std::string();
    if (operation.condition) {
        const auto previous = current->condition_observation;
        if (operation.operand_sources) {
            current->condition_observation = ExecutionConditionObservation {
                .condition = std::addressof(**operation.condition),
                .sources = *operation.operand_sources,
                .explanation = &explanation
            };
        }
        auto condition = (co_await this->value(frame, **operation.condition));
        current->condition_observation = previous;
        if (!condition) {
            co_return std::unexpected(std::move(condition.error()));
        }
        auto truth = boolean(*condition, origin);
        if (!truth) {
            co_return std::unexpected(std::move(truth.error()));
        }
        passed = *truth;
    }
    auto message = std::string();
    if (operation.message && !passed) {
        auto argument = (co_await this->value(frame, **operation.message));
        if (!argument) {
            co_return std::unexpected(std::move(argument.error()));
        }
        auto bytes = text(*argument, origin);
        if (!bytes) {
            co_return std::unexpected(std::move(bytes.error()));
        }
        message = bytes->view();
    }
    if (!passed) {
        auto event = ExecutionEvent {
            .origin = origin,
            .cause = operation.kind,
            .fields = {},
            .calls = current->calls,
            .blocks = current->blocks,
        };
        if (operation.condition_source) {
            event.fields.push_back({
                .label = "condition:",
                .text = std::string(values.spelling(*operation.condition_source)),
            });
        }
        if (!explanation.empty()) {
            event.fields.push_back({.label = "operands:", .text = std::move(explanation)});
        }
        if (operation.message) {
            event.fields.push_back({.label = "message:", .text = std::move(message)});
        }
        if (auto checked = account_text(execution_message_size(event), origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (event.termination() == ExecutionTermination::Continue) {
            context.report(event);
        } else {
            co_return std::unexpected(halt(std::move(event)));
        }
    }
    co_return ExecutionVoid {};
}
