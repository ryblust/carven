module carven:semantic.semir.callable.impl;

import :semantic.semir.callable;
import :support.invariant;
import std;

auto CallableAdaptation::borrows_storage() const noexcept -> bool {
    return kind == CallableAdaptationKind::BorrowObject;
}

auto direct_call_target(const DeclarationStore& declarations, const SemCall& call) noexcept
    -> std::optional<CallableID> {
    const auto* target = call.target ? std::get_if<CallableID>(&*call.target) : nullptr;
    if (!target
        || std::holds_alternative<ClosureBodyImplementation>(
            declarations.callable(*target).implementation
        )) {
        return std::nullopt;
    }
    return *target;
}

auto callable_identity(const SemIRProgram& program, TypeID type) noexcept
    -> std::optional<CallableID> {
    const auto& value = program.types().type(type).value;
    if (const auto* function = std::get_if<FunctionTypeValue>(&value)) {
        return function->callable;
    }
    if (const auto* closure = std::get_if<ClosureTypeValue>(&value)) {
        return closure->callable;
    }
    return std::nullopt;
}

auto query_callable_adaptation(
    const ExecutionValueAccess& values,
    ConstructionTypeRef from,
    ConstructionTypeRef to
) noexcept -> std::optional<CallableAdaptation> {
    const auto copy =
        CallableAdaptation {.kind = CallableAdaptationKind::CopyTarget, .callable = std::nullopt};
    const auto array_type = [&](ConstructionTypeRef type) noexcept {
        return visit_execution_type(
            values,
            type,
            [](const auto& shape) static noexcept -> std::optional<ConstructionArrayTypeValue> {
                using Shape = std::remove_cvref_t<decltype(shape)>;
                if constexpr (std::same_as<Shape, ArrayTypeValue>
                              || std::same_as<Shape, ConstructionArrayTypeValue>
                              || std::same_as<Shape, GenericArrayType>) {
                    return ConstructionArrayTypeValue {
                        .element = shape.element,
                        .extent = shape.extent
                    };
                }
                return std::nullopt;
            }
        );
    };
    for (;;) {
        from = execution_type_reference(values, from);
        to = execution_type_reference(values, to);
        if (from == to) {
            return copy;
        }
        const auto array = array_type(to);
        if (!array) {
            break;
        }
        const auto source = array_type(from);
        if (!source || source->extent != array->extent) {
            invariant_violation("callable array adaptation requires matching shapes");
        }
        if (array->extent == 0u) {
            return copy;
        }
        from = source->element;
        to = array->element;
    }
    const auto view_failures = [&](ConstructionTypeRef type) noexcept {
        return visit_execution_type(
            values,
            type,
            [&](const auto& shape) noexcept -> std::optional<ConstructionFailureRef> {
                using Shape = std::remove_cvref_t<decltype(shape)>;
                if constexpr (std::same_as<Shape, CallableViewTypeValue>) {
                    return values.callable_signature_copy(shape.signature).failures;
                } else if constexpr (std::same_as<Shape, ConstructionCallableViewTypeValue>
                                     || std::same_as<Shape, GenericCallableViewType>) {
                    return shape.failures;
                }
                return std::nullopt;
            }
        );
    };
    const auto target_failures = view_failures(to);
    if (!target_failures) {
        // Non-callable leaves have already passed invariant storage checking.
        return copy;
    }
    if (const auto source_failures = view_failures(from)) {
        const auto* target_set = std::get_if<FailureSetID>(&*target_failures);
        if (*source_failures == *target_failures
            || (target_set && values.failure_set_copy(*target_set).members.empty())) {
            return copy;
        }
        if (!target_set || !std::holds_alternative<FailureSetID>(*source_failures)) {
            return std::nullopt;
        }
        return CallableAdaptation {
            .kind = CallableAdaptationKind::BorrowObject,
            .callable = std::nullopt
        };
    }
    return visit_execution_type(
        values,
        from,
        [&](const auto& shape) noexcept -> std::optional<CallableAdaptation> {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, FunctionTypeValue>
                          || std::same_as<Shape, ClosureTypeValue>) {
                return CallableAdaptation {
                    .kind = std::same_as<Shape, FunctionTypeValue>
                        ? CallableAdaptationKind::FunctionTarget
                        : CallableAdaptationKind::BorrowObject,
                    .callable = shape.callable
                };
            } else if constexpr (std::same_as<Shape, GenericClosureType>) {
                return CallableAdaptation {
                    .kind = CallableAdaptationKind::BorrowObject,
                    .callable = std::get<GenericTypeID>(from),
                };
            }
            invariant_violation("callable adaptation requires a callable source");
        }
    );
}

auto callable_adaptation(const SemIRProgram& program, TypeID from, TypeID to) noexcept
    -> CallableAdaptation {
    auto result = query_callable_adaptation(ProgramValues(program), from, to);
    if (!result) {
        invariant_violation("published callable adaptation has unresolved copy identity");
    }
    if (result->borrows_storage() && result->callable) {
        const auto body =
            program.declarations().body_for_callable(std::get<CallableID>(*result->callable));
        if (!body) {
            invariant_violation("closure adaptation requires a completed body");
        }
        if (program.bodies().body(*body).inputs().captures.empty()) {
            result->kind = CallableAdaptationKind::StatelessClosure;
        }
    }
    return *result;
}
