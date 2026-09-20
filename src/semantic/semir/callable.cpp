module carven:semantic.semir.callable.impl;

import :semantic.semir.callable;
import :support.invariant;
import std;

auto CallableAdaptation::borrows_storage() const noexcept -> bool {
    return kind == CallableAdaptationKind::BorrowObject;
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

auto callable_adaptation(const SemIRProgram& program, TypeID from, TypeID to) noexcept
    -> CallableAdaptation {
    if (from == to) {
        return {.kind = CallableAdaptationKind::CopyTarget, .callable = std::nullopt};
    }
    while (const auto* array = std::get_if<ArrayTypeValue>(&program.types().type(to).value)) {
        const auto* source = std::get_if<ArrayTypeValue>(&program.types().type(from).value);
        if (source == nullptr || source->extent != array->extent) {
            invariant_violation("callable array adaptation requires matching shapes");
        }
        from = source->element;
        to = array->element;
    }
    if (!std::holds_alternative<CallableViewTypeValue>(program.types().type(to).value)) {
        invariant_violation("callable adaptation requires a view destination");
    }
    const auto& source = program.types().type(from).value;
    const auto callable = callable_identity(program, from);
    if (std::holds_alternative<FunctionTypeValue>(source)) {
        return {.kind = CallableAdaptationKind::FunctionTarget, .callable = callable};
    }
    if (std::holds_alternative<ClosureTypeValue>(source)) {
        const auto body = program.declarations().body_for_callable(*callable);
        if (!body) {
            invariant_violation("closure adaptation requires a completed body");
        }
        if (program.bodies().body(*body).inputs().captures.empty()) {
            return {.kind = CallableAdaptationKind::StatelessClosure, .callable = callable};
        }
    } else if (!std::holds_alternative<CallableViewTypeValue>(source)) {
        invariant_violation("callable adaptation requires a callable source");
    }
    return {.kind = CallableAdaptationKind::BorrowObject, .callable = callable};
}
