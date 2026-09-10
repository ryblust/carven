module carven:backend.realization.binding.impl;

import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.constant;
import :backend.realization.realizer;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyRealizer::binding_expression(LocalBindingID id) noexcept -> TargetExpr {
    auto result = name_expression(binding_names.at(id));
    if (const auto* capture = std::get_if<CaptureBindingStorage>(&metadata.binding(id).storage);
        capture != nullptr && capture->mode == CaptureMode::Write) {
        return call_member(std::move(result), "get", {});
    }
    if (const auto found = delayed_bindings.find(id); found != delayed_bindings.end()) {
        return dereference_expression(name_expression(found->second.name));
    }
    return result;
}

auto BodyRealizer::declare_binding(
    LocalBindingID id,
    TargetExpr initializer,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto& binding = metadata.binding(id);
    const auto& owner = std::get<OwnerBindingStorage>(binding.storage);
    const auto type = context.lower_type(binding.type);
    const auto* pointer =
        std::get_if<PointerTypeValue>(&context.semantic().types().type(binding.type).value);
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = owner.writable || taken_bindings.contains(id)
                ? TargetVariableBinding::MutableValue
                : TargetVariableBinding::ConstValue,
            .maybe_unused = true,
            .name = binding_names.at(id),
            .type = type,
            .initializer = std::move(initializer),
            .preserve_pointer_access = pointer != nullptr && pointer->access == PointerAccess::Write
        }
    ));
}

auto BodyRealizer::declare_deferred(
    const LoweringDeferredStorage& storage,
    bool maybe_unused,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto type = context.target().intern_type(
        {.value =
             TargetIntrinsicType {
                 .symbol = TargetSymbol::RuntimeDeferredResult,
                 .type_argument_ids = {storage.value_type}
             },
         .const_qualified = false}
    );
    destination.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = maybe_unused,
            .name = storage.name,
            .type = type,
            .initializer = TargetExpr {
                .value =
                    TargetConstructionExpr {.type = type, .initializer = std::vector<TargetExpr>()}
            }
        }
    ));
}
