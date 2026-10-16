module carven:semantic.evaluation.executor.impl;

import :semantic.evaluation.display;
import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.semir.callable;
import :support.invariant;
import std;

namespace {

auto sequence_element(const ExecutionValueAccess& values, ConstructionTypeRef reference) noexcept
    -> std::optional<ConstructionTypeRef> {
    return visit_execution_type(
        values,
        reference,
        [](const auto& shape) static noexcept -> std::optional<ConstructionTypeRef> {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, ArrayTypeValue>
                          || std::same_as<Shape, SliceTypeValue>
                          || std::same_as<Shape, VectorTypeValue>
                          || std::same_as<Shape, ConstructionArrayTypeValue>
                          || std::same_as<Shape, ConstructionSliceTypeValue>
                          || std::same_as<Shape, GenericArrayType>
                          || std::same_as<Shape, GenericSliceType>
                          || std::same_as<Shape, GenericVectorType>) {
                return shape.element;
            }
            return std::nullopt;
        }
    );
}


} // namespace

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
        case ConstantEvaluationFailure::InvalidClampBounds:
            return trap(origin, ExecutionReason::Assertion, std::move(message));
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
    const auto result = execution_equal(values, left, right, steps, limits.steps, &memory);
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
            case ExecutionComparisonFailure::UnknownAddress:
                return std::unexpected(fail(
                    origin,
                    ExecutionReason::Evaluation,
                    "pointer address relation is not known during execution"
                ));
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

auto SemanticExecutor::check_aggregate_size(
    ConstructionTypeRef type,
    ProgramOriginID origin
) noexcept -> ExecutionResult<void> {
    const auto result = shapes.get(type);
    if (result && result->exact && result->elements > maximum_constant_aggregate_elements) {
        return std::unexpected(
            fail(origin, ExecutionReason::Limit, "aggregate exceeds its element or nesting limit")
        );
    }
    return {};
}

auto SemanticExecutor::check_value_size(
    const ExecutionValue& value,
    ProgramOriginID origin
) noexcept -> ExecutionResult<void> {
    auto elements = 0uz;
    const auto fail_limit = [&]() noexcept {
        return std::unexpected(
            fail(origin, ExecutionReason::Limit, "aggregate exceeds its element or nesting limit")
        );
    };
    const auto account = [&](std::size_t count) noexcept -> ExecutionResult<void> {
        if (count > maximum_constant_aggregate_elements - elements) {
            return fail_limit();
        }
        elements += count;
        return {};
    };
    const auto inspect = [&](this auto&& self,
                             const ExecutionValue& current,
                             std::size_t depth) noexcept -> ExecutionResult<void> {
        if (depth > maximum_constant_aggregate_depth) {
            return fail_limit();
        }
        const auto compound = execution_compound_view(values, current);
        if (!compound) {
            return {};
        }
        if (depth >= maximum_constant_aggregate_depth) {
            return fail_limit();
        }
        if (const auto shape = shapes.get(compound->type); shape && shape->exact) {
            if (shape->depth > maximum_constant_aggregate_depth - depth) {
                return fail_limit();
            }
            return account(shape->elements);
        }
        const auto sequence = visit_execution_type(
            values,
            compound->type,
            [](const auto& shape) static noexcept -> std::optional<ConstructionTypeRef> {
                using Shape = std::remove_cvref_t<decltype(shape)>;
                if constexpr (std::same_as<Shape, VectorTypeValue>
                              || std::same_as<Shape, GenericVectorType>) {
                    return shape.element;
                }
                return std::nullopt;
            }
        );
        if (sequence) {
            if (const auto child = shapes.get(*sequence); child && child->exact) {
                if (compound->size() == 0uz) {
                    return {};
                }
                if (child->depth >= maximum_constant_aggregate_depth - depth
                    || compound->size() > (maximum_constant_aggregate_elements - elements)
                            / (child->elements + 1uz)) {
                    return fail_limit();
                }
                return account(compound->size() * (child->elements + 1uz));
            }
        }
        // Dynamic recursive storage has no fixed type shape. Charge its actual
        // inspection to the same work bound as other execution steps.
        if (auto checked = step(origin); !checked) {
            return checked;
        }
        if (auto checked = account(compound->size()); !checked) {
            return checked;
        }
        return compound->elements.visit([&](const auto children) noexcept -> ExecutionResult<void> {
            for (auto index = 0uz; index < children.size(); ++index) {
                if (auto checked = self(children[index], depth + 1uz); !checked) {
                    return checked;
                }
            }
            return {};
        });
    };
    return inspect(value, 0uz);
}

auto SemanticExecutor::bounded_value(ExecutionValue value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionValue> {
    if (auto checked = check_value_size(value, origin); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    return value;
}

auto SemanticExecutor::retained_storage(ConstantID id, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionPlace> {
    if (const auto found = retained_backings.find(id); found != retained_backings.end()) {
        return found->second;
    }
    auto storage = copy_value(ExecutionValue(id), origin, CopyMode::Retained);
    if (!storage) {
        return std::unexpected(std::move(storage.error()));
    }
    const auto place = memory.create(std::move(*storage));
    retained_backings.emplace(id, place);
    return place;
}

auto SemanticExecutor::retained_slice(ConstantID id, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionSlice> {
    const auto& fact = values.constant(id);
    const auto* slice = std::get_if<SliceConstant>(&fact.value);
    if (!slice) {
        return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "constant is not a slice")
        );
    }
    auto backing = retained_storage(id, origin);
    if (!backing) {
        return std::unexpected(std::move(backing.error()));
    }
    return ExecutionSlice {
        .type = fact.type,
        .backing = *backing,
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

auto SemanticExecutor::copy_value(
    const ExecutionValue& source,
    ProgramOriginID origin,
    CopyMode mode
) noexcept -> ExecutionResult<ExecutionValue> {
    const auto copy = [&](this auto&& self,
                          const ExecutionValue& value,
                          std::size_t depth) noexcept -> ExecutionResult<ExecutionValue> {
        if (depth > maximum_constant_aggregate_depth) {
            return std::unexpected(
                fail(origin, ExecutionReason::Limit, "aggregate exceeds its nesting limit")
            );
        }
        if (const auto* constant = std::get_if<ConstantID>(&value); constant
            && std::holds_alternative<SliceConstant>(values.constant(*constant).value)
            && !(mode == CopyMode::Retained && depth == 0uz)) {
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
                    .type = std::get<TypeID>(compound->type),
                    .enum_case = *compound->enum_case,
                    .payload = std::move(*copied)
                };
            }
            auto capacity = copied->size();
            if (mode == CopyMode::Retained) {
                if (const auto* id = std::get_if<ConstantID>(&value)) {
                    if (const auto* vector =
                            std::get_if<VectorConstant>(&values.constant(*id).value)) {
                        capacity = vector->capacity;
                    }
                }
            }
            return ExecutionAggregateValue {
                .type = compound->type,
                .elements = std::move(*copied),
                .capacity = capacity
            };
        }
        if (const auto* constant = std::get_if<ConstantID>(&value)) {
            const auto& fact = values.constant(*constant);
            if (const auto* text = std::get_if<TextConstant>(&fact.value); text
                && values.type_copy(fact.type).value
                    == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}) {
                const auto bytes = values.spelling(text->value);
                if (auto checked = account_text(bytes.size(), origin); !checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                return ExecutionOwnedText(std::string(bytes));
            }
        }
        if (const auto* text = std::get_if<ExecutionOwnedText>(&value)) {
            const auto bytes = text->bytes();
            if (auto checked = account_text(bytes.size(), origin); !checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return ExecutionOwnedText(std::string(bytes));
        }
        if (const auto* text = std::get_if<ExecutionText>(&value); text && !text->alive(&memory)) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
            );
        }
        if (const auto* chars = std::get_if<ExecutionChars>(&value);
            chars && !chars->text.alive(&memory)) {
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

auto SemanticExecutor::deliver_value(
    ExecutionValue& value,
    ConstructionTypeRef target,
    ProgramOriginID origin,
    std::size_t depth
) noexcept -> ExecutionResult<void> {
    target = execution_type_reference(values, target);
    if (execution_value_type(values, value) == target
        && !std::holds_alternative<ExecutionAggregateValue>(value)
        && !std::holds_alternative<ExecutionEnumValue>(value)) {
        return {};
    }
    if (depth > maximum_constant_aggregate_depth) {
        return std::unexpected(
            fail(origin, ExecutionReason::Limit, "aggregate exceeds its nesting limit")
        );
    }
    if (const auto atom = execution_atom(values, value);
        atom && std::holds_alternative<NullPointerConstant>(atom->value)) {
        value = ExecutionPointer {.type = target, .target = std::nullopt};
        return {};
    }
    if (std::holds_alternative<ConstantID>(value)) {
        auto copied = copy_value(value, origin);
        if (!copied) {
            return std::unexpected(std::move(copied.error()));
        }
        value = std::move(*copied);
    }
    return value.visit([&](auto& delivered) noexcept -> ExecutionResult<void> {
        using Value = std::remove_cvref_t<decltype(delivered)>;
        if constexpr (std::same_as<Value, ExecutionPointer>
                      || std::same_as<Value, ExecutionSlice>
                      || std::same_as<Value, ExecutionFunction>) {
            delivered.type = target;
            return {};
        } else if constexpr (std::same_as<Value, ExecutionAggregateValue>
                             || std::same_as<Value, ExecutionEnumValue>) {
            auto children = execution_elements(value);
            const auto enum_case = [&]() noexcept -> std::optional<EnumCaseID> {
                if constexpr (std::same_as<Value, ExecutionEnumValue>) {
                    return delivered.enum_case;
                }
                return std::nullopt;
            }();
            const auto fields = visit_execution_type(
                values,
                target,
                [&](const auto& shape) noexcept -> std::optional<std::vector<ConstructionTypeRef>> {
                    using Shape = std::remove_cvref_t<decltype(shape)>;
                    auto fields = std::vector<ConstructionTypeRef>();
                    if constexpr (std::same_as<Shape, TupleTypeValue>
                                  || std::same_as<Shape, ConstructionTupleTypeValue>
                                  || std::same_as<Shape, GenericTupleType>) {
                        fields.assign(shape.elements.begin(), shape.elements.end());
                    } else if constexpr (std::same_as<Shape, StructTypeValue>) {
                        const auto declared = values.struct_field_types(shape.structure);
                        if (!declared) {
                            return std::nullopt;
                        }
                        fields.assign(declared->begin(), declared->end());
                    } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
                        const auto cases = values.enum_case_types(shape.enumeration);
                        if (!cases) {
                            return std::nullopt;
                        }
                        for (const auto& item : *cases) {
                            if (item.id == enum_case) {
                                fields.assign(item.payload_types.begin(), item.payload_types.end());
                                break;
                            }
                        }
                    } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
                        return values.generic_member_types(std::get<GenericTypeID>(target));
                    } else if (const auto element = sequence_element(values, target)) {
                        fields.assign(children.size(), *element);
                    } else {
                        return std::nullopt;
                    }
                    return fields;
                }
            );
            if (!fields) {
                return std::unexpected(fail(
                    origin,
                    ExecutionReason::Evaluation,
                    "aggregate member types are not known during execution"
                ));
            }
            if (fields->size() != children.size()) {
                invariant_violation("aggregate delivery lost its checked field types");
            }
            for (auto index = 0uz; index < children.size(); ++index) {
                if (auto typed =
                        deliver_value(children[index], (*fields)[index], origin, depth + 1uz);
                    !typed) {
                    return typed;
                }
            }
            if constexpr (std::same_as<Value, ExecutionAggregateValue>) {
                delivered.type = target;
            }
            return {};
        } else if constexpr (std::same_as<Value, std::unique_ptr<ExecutionColdOperation>>) {
            delivered->type = target;
            return {};
        } else {
            invariant_violation("execution value differs from its checked delivery type");
        }
    });
}

auto SemanticExecutor::type(ConstructionTypeRef source, ProgramOriginID origin) noexcept
    -> ExecutionResult<TypeID> {
    source = execution_type_reference(values, source);
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
    -> ExecutionResult<ExecutionTextObservation> {
    if (auto text = execution_text(values, value, &memory)) {
        return std::move(*text);
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
        } else if (const auto* tuple = std::get_if<SemTupleElement>(&current->value)) {
            current = &*tuple->source;
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
    if (const auto* constant = std::get_if<SemConstant>(&expression.value)) {
        co_return retained_storage(constant->constant, expression.origin);
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
        const auto requested = expression.type.construction();
        const auto* actual = memory.resolve(*address->target);
        if (actual && execution_value_type(values, *actual) != requested) {
            const auto selected = memory.access_address(values, *address->target, requested);
            if (selected) {
                co_return *selected;
            }
            const auto actual_type = execution_value_type(values, *actual);
            if (std::holds_alternative<TypeID>(actual_type)
                && std::holds_alternative<TypeID>(requested)) {
                co_return std::unexpected(fail(
                    dereference->origin,
                    ExecutionReason::Evaluation,
                    "pointer address does not select an object of the requested type"
                ));
            }
        }
        co_return *address->target;
    }
    if (const auto* tuple = std::get_if<SemTupleElement>(&expression.value)) {
        auto selected = co_await place(frame, *tuple->source);
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        if (auto projected = memory.project(std::move(*selected), tuple->index)) {
            co_return std::move(*projected);
        }
        co_return std::unexpected(fail(
            expression.origin,
            ExecutionReason::Evaluation,
            "tuple storage projection is unavailable"
        ));
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
    auto borrowed = (co_await operand(frame, expression, OperandUse::Borrow, origin));
    if (!borrowed) {
        co_return std::unexpected(std::move(borrowed.error()));
    }
    const auto selected = std::get<ExecutionPlace>(*borrowed);
    auto selected_value = located(selected, origin);
    if (!selected_value) {
        co_return std::unexpected(std::move(selected_value.error()));
    }
    const auto* storage = *selected_value;
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
    co_return ExecutionSlice {
        .type = expression.type.construction(),
        .backing = selected,
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
    auto* chars = std::get_if<ExecutionChars>(&value);
    if (auto* text = chars ? &chars->text : std::get_if<ExecutionText>(&value);
        text && text->is_borrowed()) {
        const auto bytes = text->observe(values, &memory);
        if (!bytes) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
            );
        }
        if (auto checked = account_text(bytes->size(), origin); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        *text = ExecutionText(std::string(bytes->view()));
        return value;
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
        const auto element_type = sequence_element(values, slice->type);
        if (!element_type) {
            invariant_violation("slice delivery lost its checked element type");
        }
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
            if (auto typed = deliver_value(*copied, *element_type, origin, depth + 1uz); !typed) {
                return std::unexpected(std::move(typed.error()));
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
    if (read_borrows_storage(expression.type.construction()) && expression.selects_storage()) {
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

auto SemanticExecutor::check_callable_adaptation(
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    ProgramOriginID origin
) noexcept -> ExecutionResult<void> {
    const auto adaptation = query_callable_adaptation(values, source, target);
    if (!adaptation || adaptation->borrows_storage()) {
        return std::unexpected(fail(
            origin,
            ExecutionReason::Evaluation,
            adaptation ? "borrowing a callable object is not supported in execution"
                       : "callable adaptation target identity is not known during execution"
        ));
    }
    return {};
}

auto SemanticExecutor::operand(
    ExecutionFrame& frame,
    const SemanticExpression& expression,
    OperandUse use,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionOperand> {
    if (use == OperandUse::Borrow
        || (use == OperandUse::Read && read_borrows_storage(expression.type.construction()))) {
        if (const auto* adoption = std::get_if<SemArrayAdopt>(&expression.value)) {
            const auto adaptation = query_callable_adaptation(
                values,
                adoption->source->type.construction(),
                expression.type.construction()
            );
            if (!adaptation) {
                co_return std::unexpected(fail(
                    origin,
                    ExecutionReason::Evaluation,
                    "array adaptation storage identity is not known during execution"
                ));
            }
            if (adaptation->kind == CallableAdaptationKind::CopyTarget) {
                co_return (co_await operand(frame, *adoption->source, use, origin));
            }
        }
    }
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

auto SemanticExecutor::detach_operand(ExecutionOperand operand, ProgramOriginID origin) noexcept
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
        if (const auto* builtin = std::get_if<ExecutionBuiltinCall>(&*target)) {
            if (const auto* parser =
                    std::get_if<NumberParseImplementation>(&builtin->implementation)) {
                if (arguments.size() != 1uz) {
                    invariant_violation("number parser requires one argument");
                }
                auto input = detach_operand(std::move(arguments.front()), origin);
                if (!input) {
                    co_return std::unexpected(std::move(input.error()));
                }
                auto bytes = text(*input, origin);
                if (!bytes) {
                    co_return std::unexpected(std::move(bytes.error()));
                }
                const auto parsed = parse_number_constant(
                    values,
                    std::get<BuiltinTypeValue>(values.type_copy(builtin->result).value).kind,
                    bytes->view()
                );
                if (parsed) {
                    co_return ExecutionValue(*constant_atom(*parsed));
                }
                co_return std::unexpected(
                    ExecutionFailure {ExecutionSourceFailure {
                        .type = values.constant(parser->invalid).type,
                        .payload = std::make_shared<const ExecutionValue>(
                            *constant_atom(values.constant(parser->invalid))
                        ),
                        .origin = origin,
                        .calls = current->calls,
                        .blocks = current->blocks,
                    }}
                );
            }
            const auto provided = std::get<ProvidedFunctionImplementation>(builtin->implementation);
            co_return co_await provided_call(
                provided.function,
                builtin->result,
                std::move(arguments),
                origin
            );
        }
        const auto& body = std::get<ExecutionBody>(*target);
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

auto SemanticExecutor::root_expression(
    const SemanticExpression& source,
    std::optional<ExecutionBody> body
) noexcept -> ExecutionTask<ExecutionValue> {
    auto frame = ExecutionFrame {
        .body = body,
        .slots = std::vector<ExecutionSlot>(body ? body->binding_count() : 0uz),
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
    reference = execution_type_reference(values, reference);
    if (const auto found = storage_reads.find(reference); found != storage_reads.end()) {
        return found->second;
    }
    const auto borrows = values.read_borrows_storage(reference);
    storage_reads.emplace(reference, borrows);
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
