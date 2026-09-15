module carven:semantic.evaluation.executor.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import std;

SemanticExecutor::SemanticExecutor(
    ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    ExecutionLimits limits
) noexcept
    : values(values),
      context(context),
      limits(limits),
      shapes(values) {}

auto SemanticExecutor::fail(
    ProgramOriginID origin,
    DiagnosticCode code,
    std::string message
) noexcept -> ExecutionFailure {
    context.report(
        ExecutionDiagnostic {
            .origin = origin,
            .code = code,
            .message = std::move(message),
            .calls = calls,
        }
    );
    return ExecutionFailure {};
}

auto SemanticExecutor::step(ProgramOriginID origin) noexcept -> ExecutionResult<void> {
    if (steps >= limits.steps) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            std::format("execution exceeded {} execution steps", limits.steps)
        ));
    }
    ++steps;
    return {};
}

auto SemanticExecutor::equal(
    const ExecutionValue& left,
    const ExecutionValue& right,
    ProgramOriginID origin
) noexcept -> ExecutionResult<bool> {
    const auto result = execution_equal(values, left, right, steps, limits.steps);
    if (!result) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            std::format("comparison exceeded {} execution steps", limits.steps)
        ));
    }
    return *result;
}

auto SemanticExecutor::account_text(std::size_t bytes, ProgramOriginID origin) noexcept
    -> ExecutionResult<void> {
    if (bytes > maximum_constant_text_bytes || bytes > limits.text_work - text_work) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            "execution exceeded its text size or text construction budget"
        ));
    }
    text_work += bytes;
    return {};
}

auto SemanticExecutor::account_aggregate(std::size_t elements, ProgramOriginID origin) noexcept
    -> ExecutionResult<void> {
    if (elements > maximum_constant_aggregate_elements
        || elements > limits.aggregate_work - aggregate_work) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            "execution exceeded its aggregate size or construction budget"
        ));
    }
    aggregate_work += elements;
    return {};
}

auto SemanticExecutor::check_aggregate_size(TypeID type, ProgramOriginID origin) noexcept
    -> ExecutionResult<void> {
    const auto result = shapes.get(type);
    if (!result || result->elements > maximum_constant_aggregate_elements) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            "aggregate exceeds its element or nesting limit"
        ));
    }
    return {};
}

auto SemanticExecutor::own_storage(ExecutionValue value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue> {
    if (std::holds_alternative<ConstantID>(value)) {
        return copy_value(value, origin);
    }
    for (auto& element : execution_elements(value)) {
        auto stored = own_storage(std::move(element), origin);
        if (!stored) {
            return std::unexpected(stored.error());
        }
        element = std::move(*stored);
    }
    return value;
}

auto SemanticExecutor::copy_value(const ExecutionValue& source, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue> {
    const auto copy = [&](this auto&& self,
                          const ExecutionValue& value,
                          std::size_t depth) noexcept -> ExecutionResult<ExecutionValue> {
        if (depth > maximum_constant_aggregate_depth) {
            return std::unexpected(
                fail(origin, DiagnosticCode::ConstLimit, "aggregate exceeds its nesting limit")
            );
        }
        if (const auto compound = execution_compound_view(values, value)) {
            if (auto checked = check_aggregate_size(compound->type, origin); !checked) {
                return std::unexpected(checked.error());
            }
            auto copied = std::visit(
                [&](const auto children) noexcept -> ExecutionResult<std::vector<ExecutionValue>> {
                    if (auto checked = account_aggregate(children.size(), origin); !checked) {
                        return std::unexpected(checked.error());
                    }
                    auto elements = std::vector<ExecutionValue>();
                    elements.reserve(children.size());
                    for (const auto& child : children) {
                        auto copy = self(child, depth + 1uz);
                        if (!copy) {
                            return std::unexpected(copy.error());
                        }
                        elements.push_back(std::move(*copy));
                    }
                    return elements;
                },
                compound->elements
            );
            if (!copied) {
                return std::unexpected(copied.error());
            }
            if (compound->enum_case) {
                return ExecutionEnumValue {
                    .type = compound->type,
                    .enum_case = *compound->enum_case,
                    .payload = std::move(*copied)
                };
            }
            return ExecutionAggregateValue {.type = compound->type, .elements = std::move(*copied)};
        }
        if (const auto* text = std::get_if<ExecutionOwnedText>(&value)) {
            if (auto checked = account_text(text->bytes.size(), origin); !checked) {
                return std::unexpected(checked.error());
            }
        }
        return value;
    };
    return copy(source, 0);
}

auto SemanticExecutor::type(ConstructionTypeRef source, ProgramOriginID origin) noexcept
    -> ExecutionResult<TypeID> {
    if (const auto* concrete = std::get_if<TypeID>(&source)) {
        return *concrete;
    }
    return std::unexpected(
        fail(origin, DiagnosticCode::ConstEvaluation, "execution requires a concrete Carven type")
    );
}

auto SemanticExecutor::read_fact(const ExecutionValue& value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ConstantFact> {
    if (const auto atom = execution_atom(values, value)) {
        return constant_fact(*atom);
    }
    return std::unexpected(
        fail(origin, DiagnosticCode::ConstEvaluation, "operation requires a scalar value")
    );
}

auto SemanticExecutor::text(const ExecutionValue& value, ProgramOriginID origin) noexcept
    -> ExecutionResult<std::string_view> {
    if (const auto text = execution_text(values, value)) {
        return *text;
    }
    return std::unexpected(fail(origin, DiagnosticCode::ConstEvaluation, "expected text"));
}

auto SemanticExecutor::boolean(const ExecutionValue& value, ProgramOriginID origin) noexcept
    -> ExecutionResult<bool> {
    auto fact = read_fact(value, origin);
    if (!fact) {
        return std::unexpected(fact.error());
    }
    if (const auto* boolean = std::get_if<BooleanConstant>(&fact->value)) {
        return boolean->value;
    }
    return std::unexpected(fail(origin, DiagnosticCode::ConstEvaluation, "expected bool"));
}

auto SemanticExecutor::finish(
    std::expected<ConstantFact, ConstantEvaluationFailure> result,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    if (result) {
        if (const auto atom = constant_atom(*result)) {
            return *atom;
        }
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstEvaluation,
            "scalar operation produced aggregate storage"
        ));
    }
    const auto diagnostic = constant_evaluation_diagnostic(result.error());
    return std::unexpected(fail(
        origin,
        diagnostic ? diagnostic->code : DiagnosticCode::ConstEvaluation,
        diagnostic ? std::string(diagnostic->message) : "operation is not supported in execution"
    ));
}

auto SemanticExecutor::slot_value(
    ExecutionFrame& frame,
    std::size_t index,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue*> {
    auto& slot = frame.slots.at(index);
    if (auto* value = std::get_if<ExecutionValue>(&slot)) {
        return value;
    }
    if (const auto* alias = std::get_if<ExecutionPlace>(&slot)) {
        return located(frame, *alias, origin);
    }
    const auto taken = std::holds_alternative<ExecutionTaken>(slot);
    return std::unexpected(fail(
        origin,
        taken ? DiagnosticCode::AccessUnavailable : DiagnosticCode::ConstEvaluation,
        taken ? "execution read a binding after its value was transferred"
              : "execution read an uninitialized binding"
    ));
}

auto SemanticExecutor::local_place(const SemanticExpression& expression) noexcept -> bool {
    auto* current = &expression;
    while (true) {
        if (const auto* index = std::get_if<SemIndex>(&current->value)) {
            current = &*index->source;
        } else if (const auto* field = std::get_if<SemField>(&current->value)) {
            current = &*field->source;
        } else {
            break;
        }
    }
    return std::holds_alternative<SemBinding>(current->value);
}

auto SemanticExecutor::place(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
    -> ExecutionResult<ExecutionPlace> {
    if (auto checked = step(expression.origin); !checked) {
        return std::unexpected(checked.error());
    }
    if (const auto* name = std::get_if<SemBinding>(&expression.value)) {
        if (const auto* alias =
                std::get_if<ExecutionPlace>(&frame.slots.at(name->binding.index()))) {
            return *alias;
        }
        return ExecutionPlace {.slot = name->binding.index(), .path = {}};
    }
    if (const auto* field = std::get_if<SemField>(&expression.value)) {
        auto selected = place(frame, *field->source);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        selected->path.push_back(field->field.field_index);
        return selected;
    }
    if (const auto* index = std::get_if<SemIndex>(&expression.value)) {
        auto selected = place(frame, *index->source);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        auto subscript = value(frame, *index->index);
        if (!subscript) {
            return std::unexpected(subscript.error());
        }
        auto receiver = located(frame, *selected, expression.origin);
        if (!receiver) {
            return std::unexpected(receiver.error());
        }
        const auto* aggregate = std::get_if<ExecutionAggregateValue>(*receiver);
        if (aggregate == nullptr) {
            return std::unexpected(fail(
                expression.origin,
                DiagnosticCode::ConstEvaluation,
                "indexing requires sequence storage"
            ));
        }
        auto position = offset(*subscript, aggregate->elements.size(), index->index->origin);
        if (!position) {
            return std::unexpected(position.error());
        }
        selected->path.push_back(*position);
        return selected;
    }
    return std::unexpected(
        fail(expression.origin, DiagnosticCode::ConstEvaluation, "mutation requires local storage")
    );
}

auto SemanticExecutor::located(
    ExecutionFrame& frame,
    const ExecutionPlace& place,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue*> {
    auto result = slot_value(frame, place.slot, origin);
    if (!result) {
        return result;
    }
    for (const auto index : place.path) {
        auto* aggregate = std::get_if<ExecutionAggregateValue>(*result);
        if (aggregate == nullptr || index >= aggregate->elements.size()) {
            return std::unexpected(
                fail(origin, DiagnosticCode::ConstEvaluation, "storage projection is unavailable")
            );
        }
        result = &aggregate->elements[index];
    }
    return result;
}

auto SemanticExecutor::offset(
    const ExecutionValue& value,
    std::size_t extent,
    ProgramOriginID origin
) noexcept -> ExecutionResult<std::size_t> {
    auto fact = read_fact(value, origin);
    if (!fact) {
        return std::unexpected(fact.error());
    }
    const auto* integer = std::get_if<IntegerConstant>(&fact->value);
    if (integer == nullptr) {
        return std::unexpected(
            fail(origin, DiagnosticCode::ConstEvaluation, "sequence index must be an integer")
        );
    }
    if (integer->negative() || integer->magnitude() >= extent) {
        return std::unexpected(
            fail(origin, DiagnosticCode::ConstIndexBounds, "sequence index is out of bounds")
        );
    }
    return static_cast<std::size_t>(integer->magnitude());
}

auto SemanticExecutor::read_operand(
    ExecutionFrame& frame,
    const SemanticExpression& expression
) noexcept -> ExecutionResult<ExecutionOperand> {
    // The admitted non-owning types are scalar or trivially copied records.
    // Storage-bearing values use the same Read rule as ordinary lowering.
    const auto* concrete = std::get_if<TypeID>(&expression.type.construction());
    if (concrete != nullptr && read_borrows_storage(*concrete) && local_place(expression)) {
        auto selected = place(frame, expression);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        return std::move(*selected);
    }
    auto result = value(frame, expression);
    if (!result) {
        return std::unexpected(result.error());
    }
    return std::move(*result);
}

auto SemanticExecutor::materialize(
    ExecutionFrame& frame,
    ExecutionOperand operand,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    if (auto* owned = std::get_if<ExecutionValue>(&operand)) {
        return std::move(*owned);
    }
    auto source = located(frame, std::get<ExecutionPlace>(operand), origin);
    if (!source) {
        return std::unexpected(source.error());
    }
    return copy_value(**source, origin);
}

auto SemanticExecutor::invoke(
    FunctionID function,
    std::vector<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    if (calls.size() >= maximum_constant_depth) {
        return std::unexpected(
            fail(origin, DiagnosticCode::ConstLimit, "execution exceeded 128 nested calls")
        );
    }
    calls.push_back(origin);
    context.trace(
        {.kind = ExecutionTraceKind::Call,
         .origin = origin,
         .function = function,
         .depth = calls.size()}
    );
    const auto run = [&]() noexcept -> ExecutionResult<ExecutionValue> {
        if (auto checked = step(origin); !checked) {
            return std::unexpected(checked.error());
        }
        auto target = context.prepare_call(function, origin);
        if (!target) {
            if (auto* diagnostic = std::get_if<ExecutionDiagnostic>(&target.error())) {
                return std::unexpected(
                    fail(diagnostic->origin, diagnostic->code, std::move(diagnostic->message))
                );
            }
            return std::unexpected(ExecutionFailure {});
        }
        const auto& body = target->body;
        if (arguments.size() != body.parameters().size()) {
            return std::unexpected(fail(
                origin,
                DiagnosticCode::ConstEvaluation,
                "function call has the wrong number of arguments"
            ));
        }
        auto frame = ExecutionFrame {
            .body = body,
            .slots = std::vector<ExecutionSlot>(body.binding_count()),
        };
        for (auto index = 0uz; index < arguments.size(); ++index) {
            const auto actual = ConstructionTypeRef(execution_value_type(values, arguments[index]));
            if (actual != target->parameter_types[index]) {
                return std::unexpected(fail(
                    origin,
                    DiagnosticCode::TypeMismatch,
                    "function call argument has an incompatible type"
                ));
            }
            auto argument = std::move(arguments[index]);
            auto stored = own_storage(std::move(argument), origin);
            if (!stored) {
                return std::unexpected(stored.error());
            }
            frame.slots[body.parameters()[index].index()] = std::move(*stored);
        }
        auto result = region(frame, body.region());
        if (!result) {
            return std::unexpected(result.error());
        }
        return std::move(result->value);
    }();
    if (run) {
        context.trace(
            {.kind = ExecutionTraceKind::Return,
             .origin = origin,
             .function = function,
             .depth = calls.size()}
        );
    }
    calls.pop_back();
    return run;
}

auto SemanticExecutor::evaluate_root(const SemanticExpression& source) noexcept
    -> ExecutionResult<ExecutionValue> {
    auto frame = ExecutionFrame {.body = std::nullopt, .slots = {}};
    return value(frame, source);
}

auto SemanticExecutor::read_borrows_storage(TypeID type) noexcept -> bool {
    if (const auto found = storage_reads.find(type); found != storage_reads.end()) {
        return found->second;
    }
    const auto borrows = values.read_borrows_storage(type);
    storage_reads.emplace(type, borrows);
    return borrows;
}

auto SemanticExecutor::evaluate_test(const StructuredBodyDraft& body) noexcept
    -> ExecutionResult<void> {
    testing = true;
    auto frame = ExecutionFrame {
        .body = ExecutionBody(body),
        .slots = std::vector<ExecutionSlot>(body.bindings.size())
    };
    auto result = region(frame, body.region);
    if (!result || test_failed) {
        return std::unexpected(ExecutionFailure {});
    }
    return {};
}
