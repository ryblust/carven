module carven:backend.realization.binding.impl;

import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.body;
import :semantic.semir.contents;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyRealizer::needs_cleanup(TypeID type) const noexcept -> bool {
    const auto contents = context.semantic().type_contents(type);
    return contents.contains_native_value
        || contents.contains_storage_owner
        || contents.contains_closure_owner;
}

auto BodyRealizer::binding_expression(LocalBindingID id) noexcept -> TargetExpr {
    auto result = capture_names.contains(id) ? name_expression(capture_names.at(id))
                                             : name_expression(binding_locals.at(id));
    if (const auto* capture = std::get_if<CaptureBindingStorage>(&metadata.binding(id).storage);
        capture != nullptr && capture->mode == CaptureMode::Write) {
        return call_member(std::move(result), "get", {});
    }
    if (const auto found = delayed_bindings.find(id); found != delayed_bindings.end()) {
        return dereference_expression(name_expression(found->second.local));
    }
    return result;
}

auto BodyRealizer::declare_binding(
    LocalBindingID id,
    TargetExpr initializer,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto& binding = metadata.binding(id);
    // A typed aggregate initializer already fixes its exact native value type.
    const auto deduced_native = std::holds_alternative<TargetConstructionExpr>(initializer.value)
        && std::holds_alternative<CppTypeValue>(
                                    context.semantic().types().type(binding.type).value
        );
    const auto type = std::holds_alternative<TargetArrayExpr>(initializer.value) || deduced_native
        ? context.intrinsic_type(TargetSymbol::Auto)
        : context.lower_type(binding.type);
    destination.declare(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .maybe_unused = true,
            .local = binding_locals.at(id),
            .type = type,
            .initializer = std::move(initializer),
        },
        needs_cleanup(binding.type)
    );
}

auto BodyRealizer::declare_deferred(
    const LoweringDeferredStorage& storage,
    bool maybe_unused,
    LoweringStmtBuilder& destination,
    bool needs_cleanup
) noexcept -> void {
    const auto type = context.target().intern_type(
        {.value =
             TargetIntrinsicType {
                 .symbol = TargetSymbol::RuntimeDeferredResult,
                 .type_argument_ids = {storage.value_type}
             },
         .const_qualified = false}
    );
    destination.declare(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = maybe_unused,
            .local = storage.local,
            .type = type,
            .initializer =
                TargetExpr {
                    .value =
                        TargetConstructionExpr {
                            .type = type,
                            .initializer = std::vector<TargetExpr>()
                        }
                }
        },
        needs_cleanup
    );
}
