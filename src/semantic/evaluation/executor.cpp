module carven:semantic.evaluation.executor.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import std;

ConstantExecutor::ConstantExecutor(
    ConstantValueAccess& values,
    ConstantExecutionContext& context,
    ConstantExecutionLimits limits
) noexcept
    : values(values),
      context(context),
      limits(limits),
      shapes(values) {}

auto ConstantExecutor::fail(
    ProgramOriginID origin,
    DiagnosticCode code,
    std::string message
) noexcept -> ConstantExecutionFailure {
    context.report(
        ConstantExecutionDiagnostic {
            .origin = origin,
            .code = code,
            .message = std::move(message),
            .calls = calls,
        }
    );
    return ConstantExecutionFailure {};
}

auto ConstantExecutor::step(ProgramOriginID origin) noexcept -> ConstantExecutionResult<void> {
    if (steps >= limits.steps) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            std::format("constant evaluation exceeded {} execution steps", limits.steps)
        ));
    }
    ++steps;
    return {};
}

auto ConstantExecutor::equal(
    const ConstantExecutionValue& left,
    const ConstantExecutionValue& right,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<bool> {
    const auto result = constant_execution_equal(values, left, right, steps, limits.steps);
    if (!result) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            std::format("constant comparison exceeded {} execution steps", limits.steps)
        ));
    }
    return *result;
}

auto ConstantExecutor::account_text(std::size_t bytes, ProgramOriginID origin) noexcept
    -> ConstantExecutionResult<void> {
    if (bytes > maximum_constant_text_bytes || bytes > limits.text_work - text_work) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            "constant evaluation exceeded its text size or text construction budget"
        ));
    }
    text_work += bytes;
    return {};
}

auto ConstantExecutor::account_aggregate(std::size_t elements, ProgramOriginID origin) noexcept
    -> ConstantExecutionResult<void> {
    if (elements > maximum_constant_aggregate_elements
        || elements > limits.aggregate_work - aggregate_work) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            "constant evaluation exceeded its aggregate size or construction budget"
        ));
    }
    aggregate_work += elements;
    return {};
}

auto ConstantExecutor::check_aggregate_size(TypeID type, ProgramOriginID origin) noexcept
    -> ConstantExecutionResult<void> {
    const auto result = shapes.get(type);
    if (!result || result->elements > maximum_constant_aggregate_elements) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            "constant aggregate exceeds its element or nesting limit"
        ));
    }
    return {};
}

auto ConstantExecutor::own_storage(ConstantExecutionValue value, ProgramOriginID origin) noexcept
    -> ConstantExecutionResult<ConstantExecutionValue> {
    if (std::holds_alternative<ConstantID>(value)) {
        return copy_value(value, origin);
    }
    for (auto& element : constant_execution_elements(value)) {
        auto stored = own_storage(std::move(element), origin);
        if (!stored) {
            return std::unexpected(stored.error());
        }
        element = std::move(*stored);
    }
    return value;
}

auto ConstantExecutor::copy_value(
    const ConstantExecutionValue& source,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    const auto copy =
        [&](this auto&& self,
            const ConstantExecutionValue& value,
            std::size_t depth) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
        if (depth > maximum_constant_aggregate_depth) {
            return std::unexpected(fail(
                origin,
                DiagnosticCode::ConstLimit,
                "constant aggregate exceeds its nesting limit"
            ));
        }
        if (const auto compound = constant_compound_view(values, value)) {
            if (auto checked = check_aggregate_size(compound->type, origin); !checked) {
                return std::unexpected(checked.error());
            }
            auto copied = std::visit(
                [&](const auto children) noexcept
                    -> ConstantExecutionResult<std::vector<ConstantExecutionValue>> {
                    if (auto checked = account_aggregate(children.size(), origin); !checked) {
                        return std::unexpected(checked.error());
                    }
                    auto elements = std::vector<ConstantExecutionValue>();
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
                return ConstantEnumValue {
                    .type = compound->type,
                    .enum_case = *compound->enum_case,
                    .payload = std::move(*copied)
                };
            }
            return ConstantAggregateValue {.type = compound->type, .elements = std::move(*copied)};
        }
        if (const auto* text = std::get_if<ConstantOwnedText>(&value)) {
            if (auto checked = account_text(text->bytes.size(), origin); !checked) {
                return std::unexpected(checked.error());
            }
        }
        return value;
    };
    return copy(source, 0);
}

auto ConstantExecutor::type(ConstructionTypeRef source, ProgramOriginID origin) noexcept
    -> ConstantExecutionResult<TypeID> {
    if (const auto* concrete = std::get_if<TypeID>(&source)) {
        return *concrete;
    }
    return std::unexpected(fail(
        origin,
        DiagnosticCode::ConstEvaluation,
        "constant execution requires a concrete Carven type"
    ));
}

auto ConstantExecutor::read_fact(
    const ConstantExecutionValue& value,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantFact> {
    if (const auto atom = constant_execution_atom(values, value)) {
        return constant_fact(*atom);
    }
    return std::unexpected(
        fail(origin, DiagnosticCode::ConstEvaluation, "operation requires a constant fact")
    );
}

auto ConstantExecutor::text(const ConstantExecutionValue& value, ProgramOriginID origin) noexcept
    -> ConstantExecutionResult<std::string_view> {
    if (const auto text = constant_execution_text(values, value)) {
        return *text;
    }
    return std::unexpected(fail(origin, DiagnosticCode::ConstEvaluation, "expected constant text"));
}

auto ConstantExecutor::boolean(const ConstantExecutionValue& value, ProgramOriginID origin) noexcept
    -> ConstantExecutionResult<bool> {
    auto fact = read_fact(value, origin);
    if (!fact) {
        return std::unexpected(fact.error());
    }
    if (const auto* boolean = std::get_if<BooleanConstant>(&fact->value)) {
        return boolean->value;
    }
    return std::unexpected(fail(origin, DiagnosticCode::ConstEvaluation, "expected constant bool"));
}

auto ConstantExecutor::finish(
    std::expected<ConstantFact, ConstantEvaluationFailure> result,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
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
        diagnostic ? std::string(diagnostic->message)
                   : "operation is not supported in const execution"
    ));
}

auto ConstantExecutor::slot_value(
    ConstantFrame& frame,
    std::size_t index,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue*> {
    auto& slot = frame.slots.at(index);
    if (auto* value = std::get_if<ConstantExecutionValue>(&slot)) {
        return value;
    }
    if (const auto* alias = std::get_if<ConstantPlace>(&slot)) {
        return located(frame, *alias, origin);
    }
    const auto taken = std::holds_alternative<ConstantTaken>(slot);
    return std::unexpected(fail(
        origin,
        taken ? DiagnosticCode::AccessUnavailable : DiagnosticCode::ConstEvaluation,
        taken ? "constant execution read a binding after its value was transferred"
              : "constant execution read an uninitialized binding"
    ));
}

auto ConstantExecutor::local_place(const SemanticExpression& expression) noexcept -> bool {
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

auto ConstantExecutor::place(ConstantFrame& frame, const SemanticExpression& expression) noexcept
    -> ConstantExecutionResult<ConstantPlace> {
    if (auto checked = step(expression.origin); !checked) {
        return std::unexpected(checked.error());
    }
    if (const auto* name = std::get_if<SemBinding>(&expression.value)) {
        if (const auto* alias =
                std::get_if<ConstantPlace>(&frame.slots.at(name->binding.index()))) {
            return *alias;
        }
        return ConstantPlace {.slot = name->binding.index(), .path = {}};
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
        const auto* aggregate = std::get_if<ConstantAggregateValue>(*receiver);
        if (aggregate == nullptr) {
            return std::unexpected(fail(
                expression.origin,
                DiagnosticCode::ConstEvaluation,
                "constant indexing requires sequence storage"
            ));
        }
        auto position = offset(*subscript, aggregate->elements.size(), index->index->origin);
        if (!position) {
            return std::unexpected(position.error());
        }
        selected->path.push_back(*position);
        return selected;
    }
    return std::unexpected(fail(
        expression.origin,
        DiagnosticCode::ConstEvaluation,
        "constant mutation requires local storage"
    ));
}

auto ConstantExecutor::located(
    ConstantFrame& frame,
    const ConstantPlace& place,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue*> {
    auto result = slot_value(frame, place.slot, origin);
    if (!result) {
        return result;
    }
    for (const auto index : place.path) {
        auto* aggregate = std::get_if<ConstantAggregateValue>(*result);
        if (aggregate == nullptr || index >= aggregate->elements.size()) {
            return std::unexpected(fail(
                origin,
                DiagnosticCode::ConstEvaluation,
                "constant storage projection is unavailable"
            ));
        }
        result = &aggregate->elements[index];
    }
    return result;
}

auto ConstantExecutor::offset(
    const ConstantExecutionValue& value,
    std::size_t extent,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<std::size_t> {
    auto fact = read_fact(value, origin);
    if (!fact) {
        return std::unexpected(fact.error());
    }
    const auto* integer = std::get_if<IntegerConstant>(&fact->value);
    if (integer == nullptr) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstEvaluation,
            "constant sequence index must be an integer"
        ));
    }
    if (integer->negative() || integer->magnitude() >= extent) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstIndexBounds,
            "constant sequence index is out of bounds"
        ));
    }
    return static_cast<std::size_t>(integer->magnitude());
}

auto ConstantExecutor::read_operand(
    ConstantFrame& frame,
    const SemanticExpression& expression
) noexcept -> ConstantExecutionResult<ConstantOperand> {
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

auto ConstantExecutor::materialize(
    ConstantFrame& frame,
    ConstantOperand operand,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    if (auto* owned = std::get_if<ConstantExecutionValue>(&operand)) {
        return std::move(*owned);
    }
    auto source = located(frame, std::get<ConstantPlace>(operand), origin);
    if (!source) {
        return std::unexpected(source.error());
    }
    return copy_value(**source, origin);
}

auto ConstantExecutor::invoke(
    FunctionID function,
    std::vector<ConstantExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    if (calls.size() >= maximum_constant_depth) {
        return std::unexpected(fail(
            origin,
            DiagnosticCode::ConstLimit,
            "constant evaluation exceeded 128 nested calls"
        ));
    }
    calls.push_back(origin);
    const auto run = [&]() noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
        if (auto checked = step(origin); !checked) {
            return std::unexpected(checked.error());
        }
        auto target = context.prepare_call(function, origin);
        if (!target) {
            if (auto* diagnostic = std::get_if<ConstantExecutionDiagnostic>(&target.error())) {
                return std::unexpected(
                    fail(diagnostic->origin, diagnostic->code, std::move(diagnostic->message))
                );
            }
            return std::unexpected(ConstantExecutionFailure {});
        }
        const auto& body = target->body;
        if (arguments.size() != body.inputs.parameters.size()) {
            return std::unexpected(fail(
                origin,
                DiagnosticCode::ConstEvaluation,
                "constant call has the wrong number of arguments"
            ));
        }
        auto frame = ConstantFrame {
            .body = &body,
            .slots = std::vector<ConstantSlot>(body.bindings.size()),
        };
        for (auto index = 0uz; index < arguments.size(); ++index) {
            const auto actual =
                ConstructionTypeRef(constant_execution_value_type(values, arguments[index]));
            if (actual != target->parameter_types[index]) {
                return std::unexpected(fail(
                    origin,
                    DiagnosticCode::TypeMismatch,
                    "constant call argument has an incompatible type"
                ));
            }
            auto argument = std::move(arguments[index]);
            auto stored = own_storage(std::move(argument), origin);
            if (!stored) {
                return std::unexpected(stored.error());
            }
            frame.slots[body.inputs.parameters[index].index()] = std::move(*stored);
        }
        auto result = region(frame, body.region);
        if (!result) {
            return std::unexpected(result.error());
        }
        return std::move(result->value);
    }();
    calls.pop_back();
    return run;
}

auto ConstantExecutor::evaluate_root(const SemanticExpression& source) noexcept
    -> ConstantExecutionResult<ConstantExecutionValue> {
    auto frame = ConstantFrame {};
    return value(frame, source);
}

auto ConstantExecutor::read_borrows_storage(TypeID type) noexcept -> bool {
    if (const auto found = storage_reads.find(type); found != storage_reads.end()) {
        return found->second;
    }
    const auto borrows = values.read_borrows_storage(type);
    storage_reads.emplace(type, borrows);
    return borrows;
}

auto ConstantExecutor::evaluate_test(const StructuredBodyDraft& body) noexcept
    -> ConstantExecutionResult<void> {
    testing = true;
    auto frame =
        ConstantFrame {.body = &body, .slots = std::vector<ConstantSlot>(body.bindings.size())};
    auto result = region(frame, body.region);
    if (!result || test_failed) {
        return std::unexpected(ConstantExecutionFailure {});
    }
    return {};
}
