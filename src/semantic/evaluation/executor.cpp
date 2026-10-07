module carven:semantic.evaluation.executor.impl;

import :semantic.evaluation.display;
import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :support.invariant;
import std;

SemanticExecutor::SemanticExecutor(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    ExecutionLimits limits
) noexcept
    : values(values),
      context(context),
      limits(limits),
      shapes(values) {}

auto SemanticExecutor::fail(
    ProgramOriginID origin,
    ExecutionReason reason,
    std::string message,
    std::vector<ExecutionReportField> fields
) noexcept -> ExecutionFailure {
    return halt(
        ExecutionEvent {
            .origin = origin,
            .cause =
                ExecutionIssue {
                    .reason = reason,
                    .message = std::move(message),
                    .termination = ExecutionTermination::StopRoot,
                },
            .fields = std::move(fields),
            .calls = current->calls,
            .blocks = current->blocks,
        }
    );
}

auto SemanticExecutor::halt(ExecutionEvent event) noexcept -> ExecutionFailure {
    if (event.termination() == ExecutionTermination::Continue) {
        invariant_violation("a continuing report cannot stop execution");
    }
    if (stopped) {
        return *stopped;
    }
    context.report(event);
    return stop(ExecutionHalt {.event = std::move(event)});
}

auto SemanticExecutor::trap(
    ProgramOriginID origin,
    ExecutionReason reason,
    std::string message,
    std::vector<ExecutionReportField> fields
) noexcept -> ExecutionFailure {
    return halt(
        ExecutionEvent {
            .origin = origin,
            .cause =
                ExecutionIssue {
                    .reason = reason,
                    .message = std::move(message),
                    .termination = ExecutionTermination::Abort,
                },
            .fields = std::move(fields),
            .calls = current->calls,
            .blocks = current->blocks,
        }
    );
}

auto SemanticExecutor::operation_failure(
    ConstantEvaluationFailure failure,
    ProgramOriginID origin,
    std::string_view fallback
) noexcept -> ExecutionFailure {
    const auto diagnostic = constant_evaluation_diagnostic(failure);
    auto message = std::string(diagnostic ? diagnostic->message : fallback);
    switch (failure) {
        case ConstantEvaluationFailure::DivideByZero:
            return trap(origin, ExecutionReason::DivideByZero, std::move(message));
        case ConstantEvaluationFailure::ShiftOutOfRange:
            return trap(origin, ExecutionReason::ShiftOutOfRange, std::move(message));
        case ConstantEvaluationFailure::SliceOutOfBounds:
        case ConstantEvaluationFailure::SIMDOutOfBounds:
            return trap(origin, ExecutionReason::IndexBounds, std::move(message));
        case ConstantEvaluationFailure::IntegerOverflow:
        case ConstantEvaluationFailure::IntegerLiteralOutOfRange:
        case ConstantEvaluationFailure::FloatingLiteralOutOfRange:
            return fail(origin, ExecutionReason::Overflow, std::move(message));
        case ConstantEvaluationFailure::IntegerLiteralNotRepresentable:
            return fail(origin, ExecutionReason::LiteralRange, std::move(message));
        case ConstantEvaluationFailure::OperandNotConstant:
        case ConstantEvaluationFailure::UnsupportedOperation:
        case ConstantEvaluationFailure::InvalidOperation:
            return fail(origin, ExecutionReason::Evaluation, std::move(message));
    }
    std::unreachable();
}

auto SemanticExecutor::step(ProgramOriginID origin) noexcept -> ExecutionResult<void> {
    if (steps >= limits.steps) {
        return std::unexpected(fail(
            origin,
            ExecutionReason::Limit,
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
        switch (result.error()) {
            case ExecutionComparisonFailure::StepLimit:
                return std::unexpected(fail(
                    origin,
                    ExecutionReason::Limit,
                    std::format("comparison exceeded {} execution steps", limits.steps)
                ));
            case ExecutionComparisonFailure::ExpiredText:
                return std::unexpected(
                    fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
                );
            case ExecutionComparisonFailure::Unsupported:
                return std::unexpected(fail(
                    origin,
                    ExecutionReason::Evaluation,
                    "comparison is not supported in execution"
                ));
        }
        std::unreachable();
    }
    return *result;
}

auto SemanticExecutor::account_text(std::size_t bytes, ProgramOriginID origin) noexcept
    -> ExecutionResult<void> {
    if (bytes > maximum_constant_text_bytes || bytes > limits.text_work - text_work) {
        return std::unexpected(fail(
            origin,
            ExecutionReason::Limit,
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
            ExecutionReason::Limit,
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
        return std::unexpected(
            fail(origin, ExecutionReason::Limit, "aggregate exceeds its element or nesting limit")
        );
    }
    return {};
}

auto SemanticExecutor::retained_slice(ConstantID id, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionSlice> {
    const auto& fact = values.constant(id);
    const auto* slice = std::get_if<SliceConstant>(&fact.value);
    if (slice == nullptr) {
        return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "constant is not a slice")
        );
    }
    if (const auto found = retained_slice_backings.find(id);
        found != retained_slice_backings.end()) {
        return ExecutionSlice {
            .type = fact.type,
            .backing = found->second,
            .offset = 0uz,
            .extent = slice->elements.size()
        };
    }
    if (auto checked = account_aggregate(slice->elements.size(), origin); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    auto elements = std::vector<ExecutionValue>();
    elements.reserve(slice->elements.size());
    for (const auto element : slice->elements) {
        elements.emplace_back(element);
    }
    auto storage = own_storage(
        ExecutionAggregateValue {.type = fact.type, .elements = std::move(elements)},
        origin
    );
    if (!storage) {
        return std::unexpected(std::move(storage.error()));
    }
    auto backing = memory.create(std::move(*storage));
    retained_slice_backings.emplace(id, backing);
    return ExecutionSlice {
        .type = fact.type,
        .backing = std::move(backing),
        .offset = 0uz,
        .extent = slice->elements.size()
    };
}

auto SemanticExecutor::own_storage(ExecutionValue value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue> {
    if (std::holds_alternative<ConstantID>(value)) {
        return copy_value(value, origin);
    }
    for (auto& element : execution_elements(value)) {
        auto stored = own_storage(std::move(element), origin);
        if (!stored) {
            return std::unexpected(std::move(stored.error()));
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
                fail(origin, ExecutionReason::Limit, "aggregate exceeds its nesting limit")
            );
        }
        if (const auto* constant = std::get_if<ConstantID>(&value);
            constant && std::holds_alternative<SliceConstant>(values.constant(*constant).value)) {
            auto slice = retained_slice(*constant, origin);
            if (!slice) {
                return std::unexpected(std::move(slice.error()));
            }
            return *slice;
        }
        if (std::holds_alternative<std::unique_ptr<ExecutionColdOperation>>(value)) {
            return std::unexpected(
                fail(origin, ExecutionReason::Admission, "a cold operation cannot be copied")
            );
        }
        if (const auto compound = execution_compound_view(values, value)) {
            if (auto checked = check_aggregate_size(compound->type, origin); !checked) {
                return std::unexpected(std::move(checked.error()));
            }
            auto copied = compound->elements.visit(
                [&](const auto children) noexcept -> ExecutionResult<std::vector<ExecutionValue>> {
                    if (auto checked = account_aggregate(children.size(), origin); !checked) {
                        return std::unexpected(std::move(checked.error()));
                    }
                    auto elements = std::vector<ExecutionValue>();
                    elements.reserve(children.size());
                    for (auto index = 0uz; index < children.size(); ++index) {
                        auto copy = self(children[index], depth + 1uz);
                        if (!copy) {
                            return std::unexpected(std::move(copy.error()));
                        }
                        elements.push_back(std::move(*copy));
                    }
                    return elements;
                }
            );
            if (!copied) {
                return std::unexpected(std::move(copied.error()));
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
            const auto bytes = text->bytes();
            if (auto checked = account_text(bytes.size(), origin); !checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return ExecutionOwnedText(std::string(bytes));
        }
        if (const auto* text = std::get_if<ExecutionText>(&value); text && !text->bytes()) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
            );
        }
        return value.visit([](const auto& atom) static noexcept -> ExecutionValue {
            using Atom = std::remove_cvref_t<decltype(atom)>;
            if constexpr (std::same_as<Atom, ExecutionAggregateValue>
                          || std::same_as<Atom, ExecutionEnumValue>
                          || std::same_as<Atom, ExecutionOwnedText>
                          || std::same_as<Atom, std::unique_ptr<ExecutionColdOperation>>) {
                invariant_violation("owned execution value bypassed semantic copying");
            } else {
                return atom;
            }
        });
    };
    return copy(source, 0);
}

auto SemanticExecutor::type(ConstructionTypeRef source, ProgramOriginID origin) noexcept
    -> ExecutionResult<TypeID> {
    if (const auto* concrete = std::get_if<TypeID>(&source)) {
        return *concrete;
    }
    return std::unexpected(
        fail(origin, ExecutionReason::Evaluation, "execution requires a concrete Carven type")
    );
}

auto SemanticExecutor::read_fact(const ExecutionValue& value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ConstantFact> {
    if (const auto atom = execution_atom(values, value)) {
        return constant_fact(*atom);
    }
    return std::unexpected(
        fail(origin, ExecutionReason::Evaluation, "operation requires a scalar value")
    );
}

auto SemanticExecutor::text(const ExecutionValue& value, ProgramOriginID origin) noexcept
    -> ExecutionResult<std::string_view> {
    if (const auto text = execution_text(values, value)) {
        return *text;
    }
    return std::unexpected(fail(origin, ExecutionReason::Evaluation, "expected text"));
}

auto SemanticExecutor::boolean(const ExecutionValue& value, ProgramOriginID origin) noexcept
    -> ExecutionResult<bool> {
    auto fact = read_fact(value, origin);
    if (!fact) {
        return std::unexpected(std::move(fact.error()));
    }
    if (const auto* boolean = std::get_if<BooleanConstant>(&fact->value)) {
        return boolean->value;
    }
    return std::unexpected(fail(origin, ExecutionReason::Evaluation, "expected bool"));
}

auto SemanticExecutor::finish(
    std::expected<ConstantFact, ConstantEvaluationFailure> result,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    if (result) {
        if (const auto atom = constant_atom(*result)) {
            return *atom;
        }
        return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "scalar operation produced aggregate storage")
        );
    }
    return std::unexpected(
        operation_failure(result.error(), origin, "operation is not supported in execution")
    );
}

auto SemanticExecutor::bind(ExecutionFrame& frame, std::size_t slot, ExecutionValue value) noexcept
    -> void {
    release(frame, slot);
    frame.slots[slot] = ExecutionOwner {.place = memory.create(std::move(value))};
}

auto SemanticExecutor::release(ExecutionFrame& frame, std::size_t slot) noexcept -> void {
    if (const auto* owner = std::get_if<ExecutionOwner>(&frame.slots[slot])) {
        memory.release(owner->place);
    }
    frame.slots[slot] = ExecutionUninitialized {};
}

auto SemanticExecutor::release_temporaries(ExecutionFrame& frame, std::size_t begin) noexcept
    -> void {
    while (frame.temporaries.size() > begin) {
        memory.release(frame.temporaries.back());
        frame.temporaries.pop_back();
    }
}

auto SemanticExecutor::release_frame(ExecutionFrame& frame) noexcept -> void {
    release_temporaries(frame, 0);
    for (auto slot = frame.slots.size(); slot != 0; --slot) {
        release(frame, slot - 1uz);
    }
}

auto SemanticExecutor::release_region(ExecutionFrame& frame, LifetimeRegionID lifetime) noexcept
    -> void {
    if (frame.body) {
        for (const auto slot : frame.body->bindings_in(lifetime)) {
            release(frame, slot);
        }
    }
}

auto SemanticExecutor::slot_value(
    ExecutionFrame& frame,
    std::size_t index,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue*> {
    const auto& slot = frame.slots.at(index);
    if (const auto* owner = std::get_if<ExecutionOwner>(&slot)) {
        return located(owner->place, origin);
    }
    if (const auto* alias = std::get_if<ExecutionPlace>(&slot)) {
        return located(*alias, origin);
    }
    const auto taken = std::holds_alternative<ExecutionTaken>(slot);
    return std::unexpected(fail(
        origin,
        taken ? ExecutionReason::Unavailable : ExecutionReason::Evaluation,
        taken ? "execution read a binding after its value was transferred"
              : "execution read an uninitialized binding"
    ));
}

// Value projection can read a retained constant child without creating an object.
// Bound storage instead supplies the child directly, without copying its owner.
auto SemanticExecutor::has_bound_storage(const SemanticExpression& expression) noexcept -> bool {
    auto* current = &expression;
    while (true) {
        if (const auto* index = std::get_if<SemIndex>(&current->value)) {
            current = &*index->source;
        } else if (const auto* field = std::get_if<SemField>(&current->value)) {
            current = &*field->source;
        } else {
            return std::holds_alternative<SemBinding>(current->value)
                || std::holds_alternative<SemDereference>(current->value);
        }
    }
}

auto SemanticExecutor::place(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
    -> ExecutionTask<ExecutionPlace> {
    if (std::holds_alternative<SemUnreachable>(expression.value)) {
        invariant_violation("execution reached a semantic edge without an entry");
    }
    if (auto checked = step(expression.origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    if (const auto* name = std::get_if<SemBinding>(&expression.value)) {
        const auto& slot = frame.slots.at(name->binding.index());
        if (const auto* alias = std::get_if<ExecutionPlace>(&slot)) {
            co_return *alias;
        }
        if (const auto* owner = std::get_if<ExecutionOwner>(&slot)) {
            co_return owner->place;
        }
        auto unavailable = slot_value(frame, name->binding.index(), expression.origin);
        co_return std::unexpected(std::move(unavailable.error()));
    }
    if (const auto* dereference = std::get_if<SemDereference>(&expression.value)) {
        auto pointer = (co_await this->value(frame, *dereference->source));
        if (!pointer) {
            co_return std::unexpected(std::move(pointer.error()));
        }
        const auto* address = std::get_if<ExecutionPointer>(&*pointer);
        if (address == nullptr || !address->target) {
            co_return std::unexpected(fail(
                dereference->origin,
                ExecutionReason::Evaluation,
                "cannot dereference a null pointer"
            ));
        }
        if (auto target = located(*address->target, dereference->origin); !target) {
            co_return std::unexpected(std::move(target.error()));
        }
        co_return *address->target;
    }
    if (const auto* field = std::get_if<SemField>(&expression.value)) {
        auto selected = (co_await place(frame, *field->source));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        auto projected = memory.project(std::move(*selected), field->field.field_index);
        if (projected) {
            co_return std::move(*projected);
        }
        co_return std::unexpected(fail(
            expression.origin,
            ExecutionReason::Evaluation,
            "storage projection is unavailable"
        ));
    }
    if (const auto* index = std::get_if<SemIndex>(&expression.value)) {
        auto sequence = (co_await sequence_view(frame, *index->source, expression.origin));
        if (!sequence) {
            co_return std::unexpected(std::move(sequence.error()));
        }
        auto subscript = (co_await this->value(frame, *index->index));
        if (!subscript) {
            co_return std::unexpected(std::move(subscript.error()));
        }
        auto position = offset(*subscript, sequence->extent, index->index->origin);
        if (!position) {
            co_return std::unexpected(std::move(position.error()));
        }
        co_return slice_element(*sequence, *position, expression.origin);
    }
    co_return std::unexpected(fail(
        expression.origin,
        ExecutionReason::Evaluation,
        "operation requires addressable storage"
    ));
}

auto SemanticExecutor::sequence_view(
    ExecutionFrame& frame,
    const SemanticExpression& expression,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionSlice> {
    if (std::holds_alternative<SemUnreachable>(expression.value)) {
        invariant_violation("execution reached a semantic edge without an entry");
    }
    auto source_type = type(expression.type.construction(), origin);
    if (!source_type) {
        co_return std::unexpected(std::move(source_type.error()));
    }
    auto selected = std::optional<ExecutionPlace>();
    auto result = std::optional<ExecutionValue>();
    if (expression.selects_storage()) {
        auto source = (co_await place(frame, expression));
        if (!source) {
            co_return std::unexpected(std::move(source.error()));
        }
        selected = std::move(*source);
    } else {
        auto evaluated = (co_await this->value(frame, expression));
        if (!evaluated) {
            co_return std::unexpected(std::move(evaluated.error()));
        }
        result = std::move(*evaluated);
    }
    auto* storage = static_cast<ExecutionValue*>(nullptr);
    if (selected) {
        auto located_value = located(*selected, origin);
        if (!located_value) {
            co_return std::unexpected(std::move(located_value.error()));
        }
        storage = *located_value;
    } else {
        storage = &*result;
    }
    if (const auto* slice = std::get_if<ExecutionSlice>(storage)) {
        if (!memory.view(*slice)) {
            co_return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "slice backing is no longer alive")
            );
        }
        co_return *slice;
    }
    if (const auto* constant = std::get_if<ConstantID>(storage);
        constant && std::holds_alternative<SliceConstant>(values.constant(*constant).value)) {
        co_return retained_slice(*constant, origin);
    }
    const auto compound = execution_compound_view(values, *storage);
    if (!compound) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "slice requires a sequence")
        );
    }
    const auto extent = compound->size();
    if (!selected) {
        auto owned = own_storage(std::move(*result), origin);
        if (!owned) {
            co_return std::unexpected(std::move(owned.error()));
        }
        selected = memory.create(std::move(*owned));
        frame.temporaries.push_back(*selected);
    }
    co_return ExecutionSlice {
        .type = *source_type,
        .backing = std::move(*selected),
        .offset = 0uz,
        .extent = extent
    };
}

auto SemanticExecutor::slice_element(
    const ExecutionSlice& slice,
    std::size_t index,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionPlace> {
    if (!memory.view(slice)) {
        return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "slice backing is no longer alive")
        );
    }
    if (index >= slice.extent) {
        return std::unexpected(trap(
            origin,
            ExecutionReason::IndexBounds,
            "sequence index is out of bounds",
            {{.label = "index:", .text = std::to_string(index)},
             {.label = "length:", .text = std::to_string(slice.extent)}}
        ));
    }
    auto selected = memory.project(slice.backing, slice.offset + index);
    if (!selected) {
        return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "slice element is unavailable")
        );
    }
    return std::move(*selected);
}

auto SemanticExecutor::detach_views(
    ExecutionValue value,
    ProgramOriginID origin,
    std::size_t depth
) noexcept -> ExecutionResult<ExecutionValue> {
    if (depth > maximum_constant_aggregate_depth) {
        return std::unexpected(
            fail(origin, ExecutionReason::Limit, "aggregate exceeds its nesting limit")
        );
    }
    if (const auto* text = std::get_if<ExecutionText>(&value); text && text->is_borrowed()) {
        const auto bytes = text->bytes();
        if (!bytes) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
            );
        }
        if (auto checked = account_text(bytes->size(), origin); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        return ExecutionText(std::string(*bytes));
    }
    if (const auto* constant = std::get_if<ConstantID>(&value);
        constant && std::holds_alternative<SliceConstant>(values.constant(*constant).value)) {
        auto slice = retained_slice(*constant, origin);
        if (!slice) {
            return std::unexpected(std::move(slice.error()));
        }
        value = std::move(*slice);
    }
    if (const auto* slice = std::get_if<ExecutionSlice>(&value)) {
        if (!memory.view(*slice)) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "slice backing is no longer alive")
            );
        }
        if (auto checked = account_aggregate(slice->extent, origin); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        auto elements = std::vector<ExecutionValue>();
        elements.reserve(slice->extent);
        for (auto index = 0uz; index < slice->extent; ++index) {
            auto selected = slice_element(*slice, index, origin);
            if (!selected) {
                return std::unexpected(std::move(selected.error()));
            }
            const auto* target = memory.resolve(*selected);
            if (target == nullptr) {
                return std::unexpected(
                    fail(origin, ExecutionReason::Evaluation, "slice element is unavailable")
                );
            }
            auto copied = copy_value(*target, origin);
            if (!copied) {
                return std::unexpected(std::move(copied.error()));
            }
            auto detached = detach_views(std::move(*copied), origin, depth + 1uz);
            if (!detached) {
                return std::unexpected(std::move(detached.error()));
            }
            elements.push_back(std::move(*detached));
        }
        return ExecutionAggregateValue {.type = slice->type, .elements = std::move(elements)};
    }
    for (auto& element : execution_elements(value)) {
        auto detached = detach_views(std::move(element), origin, depth + 1uz);
        if (!detached) {
            return std::unexpected(std::move(detached.error()));
        }
        element = std::move(*detached);
    }
    return value;
}

auto SemanticExecutor::located(const ExecutionPlace& place, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue*> {
    if (auto* value = memory.resolve(place)) {
        return value;
    }
    return std::unexpected(
        fail(origin, ExecutionReason::Evaluation, "pointer target is no longer alive")
    );
}

auto SemanticExecutor::offset(
    const ExecutionValue& value,
    std::size_t extent,
    ProgramOriginID origin
) noexcept -> ExecutionResult<std::size_t> {
    auto fact = read_fact(value, origin);
    if (!fact) {
        return std::unexpected(std::move(fact.error()));
    }
    const auto* integer = std::get_if<IntegerConstant>(&fact->value);
    if (integer == nullptr) {
        return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "sequence index must be an integer")
        );
    }
    if (integer->negative() || integer->magnitude() >= extent) {
        return std::unexpected(trap(
            origin,
            ExecutionReason::IndexBounds,
            "sequence index is out of bounds",
            {{.label = "index:",
              .text = std::format("{}{}", integer->negative() ? "-" : "", integer->magnitude())},
             {.label = "length:", .text = std::to_string(extent)}}
        ));
    }
    return static_cast<std::size_t>(integer->magnitude());
}

auto SemanticExecutor::read_operand(
    ExecutionFrame& frame,
    const SemanticExpression& expression
) noexcept -> ExecutionTask<ExecutionOperand> {
    // The admitted non-owning types are scalar or trivially copied records.
    // Storage-bearing values use the same Read rule as ordinary lowering.
    const auto* concrete = std::get_if<TypeID>(&expression.type.construction());
    if (concrete != nullptr && read_borrows_storage(*concrete) && expression.selects_storage()) {
        auto selected = (co_await place(frame, expression));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        co_return std::move(*selected);
    }
    auto result = (co_await this->value(frame, expression));
    if (!result) {
        co_return std::unexpected(std::move(result.error()));
    }
    co_return std::move(*result);
}

auto SemanticExecutor::argument_use(AccessMode access) noexcept -> OperandUse {
    switch (access) {
        case AccessMode::Read:  return OperandUse::Read;
        case AccessMode::Write: return OperandUse::Write;
        case AccessMode::Take:  return OperandUse::Value;
    }
    std::unreachable();
}

auto SemanticExecutor::operand(
    ExecutionFrame& frame,
    const SemanticExpression& expression,
    OperandUse use,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionOperand> {
    if (use == OperandUse::Write || (use == OperandUse::Borrow && expression.selects_storage())) {
        auto selected = (co_await place(frame, expression));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        co_return std::move(*selected);
    }
    if (use == OperandUse::Value) {
        auto evaluated = (co_await value(frame, expression));
        if (!evaluated) {
            co_return std::unexpected(std::move(evaluated.error()));
        }
        co_return std::move(*evaluated);
    }
    auto evaluated = (co_await read_operand(frame, expression));
    if (!evaluated) {
        co_return std::unexpected(std::move(evaluated.error()));
    }
    if (auto* temporary = std::get_if<ExecutionValue>(&*evaluated); temporary
        && (use == OperandUse::Borrow
            || read_borrows_storage(execution_value_type(values, *temporary)))) {
        auto owned = own_storage(std::move(*temporary), origin);
        if (!owned) {
            co_return std::unexpected(std::move(owned.error()));
        }
        auto selected = memory.create(std::move(*owned));
        frame.temporaries.push_back(selected);
        *evaluated = std::move(selected);
    }
    co_return evaluated;
}

auto SemanticExecutor::materialize(ExecutionOperand operand, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue> {
    if (auto* owned = std::get_if<ExecutionValue>(&operand)) {
        return std::move(*owned);
    }
    auto source = located(std::get<ExecutionPlace>(operand), origin);
    if (!source) {
        return std::unexpected(std::move(source.error()));
    }
    return copy_value(**source, origin);
}

auto SemanticExecutor::detach_argument(ExecutionOperand operand, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue> {
    auto value = materialize(std::move(operand), origin);
    if (!value) {
        return std::unexpected(std::move(value.error()));
    }
    return detach_result(std::move(*value), origin);
}

auto SemanticExecutor::call(
    CallableID callable,
    std::vector<ExecutionOperand> arguments,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto function = context.function_for_callable(callable);
    if (current->calls.size() >= maximum_constant_depth) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Limit, "execution exceeded 128 nested calls")
        );
    }
    current->calls.push_back(origin);
    context.trace(
        {.kind = ExecutionTraceKind::Call,
         .origin = origin,
         .function = function,
         .depth = current->calls.size()}
    );
    auto run = co_await [&]() noexcept -> ExecutionTask<ExecutionValue> {
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        const auto unavailable = [&](ExecutionCallFailure failure) noexcept {
            if (auto* event = std::get_if<ExecutionEvent>(&failure)) {
                event->calls = current->calls;
                event->blocks = current->blocks;
                return halt(std::move(*event));
            }
            if (auto* delivered = std::get_if<ExecutionFailure>(&failure)) {
                return stop(std::move(*delivered));
            }
            std::unreachable();
        };
        auto selected = co_await context.bind_call(callable, arguments, *this, origin);
        if (!selected) {
            co_return std::unexpected(unavailable(std::move(selected.error())));
        }
        callable = *selected;
        auto target = (co_await context.prepare_call(callable, origin));
        if (!target) {
            co_return std::unexpected(unavailable(std::move(target.error())));
        }
        const auto& body = *target;
        const auto parameters = body.parameters();
        if (arguments.size() != parameters.size()) {
            co_return std::unexpected(fail(
                origin,
                ExecutionReason::Evaluation,
                "function call has the wrong number of arguments"
            ));
        }
        auto frame = ExecutionFrame {
            .body = body,
            .slots = std::vector<ExecutionSlot>(body.binding_count()),
            .caught = {},
            .temporaries = {},
            .children = {},
        };
        const auto execute = [&]() noexcept -> ExecutionTask<ExecutionValue> {
            for (auto index = 0uz; index < arguments.size(); ++index) {
                const auto binding = parameters[index];
                auto& argument = arguments[index];
                const auto* alias = std::get_if<ExecutionPlace>(&argument);
                auto* owned = std::get_if<ExecutionValue>(&argument);
                auto selected =
                    alias ? located(*alias, origin) : ExecutionResult<ExecutionValue*>(owned);
                if (!selected) {
                    co_return std::unexpected(std::move(selected.error()));
                }
                const auto access = body.binding_access(binding);
                if (alias
                    && (access == AccessMode::Write
                        || (access == AccessMode::Read
                            && read_borrows_storage(execution_value_type(values, **selected))))) {
                    frame.slots[binding.index()] = *alias;
                } else {
                    if (access == AccessMode::Write) {
                        co_return std::unexpected(fail(
                            origin,
                            ExecutionReason::Evaluation,
                            "Write argument requires addressable storage"
                        ));
                    }
                    auto stored = owned ? own_storage(std::move(*owned), origin)
                                        : copy_value(**selected, origin);
                    if (!stored) {
                        co_return std::unexpected(std::move(stored.error()));
                    }
                    bind(frame, binding.index(), std::move(*stored));
                }
            }
            auto result = (co_await region(frame, body.region()));
            if (!result) {
                co_return std::unexpected(std::move(result.error()));
            }
            co_return std::move(result->value);
        };
        auto result = (co_await execute());
        release_frame(frame);
        co_return result;
    }();
    if (run) {
        context.trace(
            {.kind = ExecutionTraceKind::Return,
             .origin = origin,
             .function = function,
             .depth = current->calls.size()}
        );
    }
    current->calls.pop_back();
    co_return run;
}

auto SemanticExecutor::root_expression(const SemanticExpression& source) noexcept
    -> ExecutionTask<ExecutionValue> {
    auto frame = ExecutionFrame {
        .body = std::nullopt,
        .slots = {},
        .caught = {},
        .temporaries = {},
        .children = {},
    };
    auto result = (co_await this->value(frame, source));
    if (result) {
        result = detach_views(std::move(*result), source.origin);
    }
    release_frame(frame);
    co_return result;
}

auto SemanticExecutor::freeze(ExecutionValue value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ConstantID> {
    auto detached = detach_views(std::move(value), origin);
    if (!detached) {
        return std::unexpected(std::move(detached.error()));
    }
    if (const auto constant = context.freeze(std::move(*detached))) {
        return *constant;
    }
    return std::unexpected(
        fail(origin, ExecutionReason::Admission, "static value has no frozen representation")
    );
}

auto SemanticExecutor::escaped(ExecutionFailure failure) noexcept -> ExecutionFailure {
    if (const auto* cancelled = std::get_if<ExecutionCancelled>(&failure)) {
        return halt(
            ExecutionEvent {
                .origin = cancelled->origin,
                .cause =
                    ExecutionIssue {
                        .reason = ExecutionReason::Evaluation,
                        .message = "cancellation escaped execution without a consumer",
                        .termination = ExecutionTermination::StopRoot,
                    },
                .fields = {},
                .calls = cancelled->calls,
                .blocks = cancelled->blocks,
            }
        );
    }
    const auto* source = std::get_if<ExecutionSourceFailure>(&failure);
    if (!source) {
        return failure;
    }
    auto fields = std::vector<ExecutionReportField>();
    if (auto payload = display_execution_value(values, *source->payload)) {
        fields.push_back({.label = "failure:", .text = std::move(*payload)});
    }
    return halt(
        ExecutionEvent {
            .origin = source->origin,
            .cause =
                ExecutionIssue {
                    .reason = ExecutionReason::Evaluation,
                    .message = "typed failure escaped execution without recovery",
                    .termination = ExecutionTermination::StopRoot,
                },
            .fields = std::move(fields),
            .calls = source->calls,
            .blocks = source->blocks,
        }
    );
}

auto SemanticExecutor::detach_result(ExecutionValue value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue> {
    return detach_views(std::move(value), origin);
}

auto SemanticExecutor::read_borrows_storage(ConstructionTypeRef reference) noexcept -> bool {
    const auto* concrete = std::get_if<TypeID>(&reference);
    if (!concrete) {
        return false;
    }
    const auto type = *concrete;
    if (const auto found = storage_reads.find(type); found != storage_reads.end()) {
        return found->second;
    }
    const auto borrows = values.read_borrows_storage(type);
    storage_reads.emplace(type, borrows);
    return borrows;
}

auto SemanticExecutor::root_body(ExecutionBody body) noexcept -> ExecutionTask<ExecutionValue> {
    testing = body.kind() == BodyKind::Test;
    auto frame = ExecutionFrame {
        .body = body,
        .slots = std::vector<ExecutionSlot>(body.binding_count()),
        .caught = {},
        .temporaries = {},
        .children = {},
    };
    auto result = (co_await region(frame, body.region()));
    release_frame(frame);
    if (!result) {
        co_return std::unexpected(std::move(result.error()));
    }
    co_return ExecutionVoid {};
}
