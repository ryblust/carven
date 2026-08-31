module carven:semantic.analysis.call_contract.impl;

import :semantic.analysis.call_contract;
import :semantic.hir.expr;
import :semantic.hir.type;
import :support.invariant;
import std;

namespace {

auto project_callable_contract(const auto& semantic, HIRTypeID callable_type) noexcept
    -> std::optional<CallContractView> {
    const auto& type = semantic.type(callable_type).value;
    if (const auto* function = std::get_if<HIRFunctionTypeValue>(&type)) {
        if (function->callable.index() >= semantic.callables().size()) {
            return std::nullopt;
        }
        const auto& callable = semantic.callable(function->callable);
        return CallContractView {
            .parameters = callable.parameters,
            .result = callable.result,
            .failure_source = ConcreteCallableFailure {.callable = function->callable},
        };
    }
    if (const auto* closure = std::get_if<HIRClosureTypeValue>(&type)) {
        if (closure->callable.index() >= semantic.callables().size()) {
            return std::nullopt;
        }
        const auto& callable = semantic.callable(closure->callable);
        return CallContractView {
            .parameters = callable.parameters,
            .result = callable.result,
            .failure_source = ConcreteCallableFailure {.callable = closure->callable},
        };
    }
    if (const auto* reference = std::get_if<HIRFunctionRefTypeValue>(&type)) {
        if (reference->signature.index() >= semantic.callable_signatures().size()) {
            return std::nullopt;
        }
        const auto& signature = semantic.callable_signature(reference->signature);
        return CallContractView {
            .parameters = signature.parameters,
            .result = signature.result,
            .failure_source = FixedSignatureFailure {.failure_set = signature.failure_set},
        };
    }
    return std::nullopt;
}

auto project_call_contract(const auto& semantic, HIRExprID verified_call) noexcept
    -> CallContractView {
    const auto* call = std::get_if<HIRCallExpr>(&semantic.expression(verified_call).value);
    if (call == nullptr) {
        invariant_violation("call contract projection requires a verified call expression");
    }
    const auto callee_type = semantic.expression(call->callee).type;
    if (auto contract = project_callable_contract(semantic, callee_type)) {
        return *contract;
    }
    if (std::holds_alternative<HIRForeignTypeValue>(semantic.type(callee_type).value)) {
        return {
            .parameters = {},
            .result = semantic.expression(verified_call).type,
            .failure_source = ForeignCallableFailure {},
        };
    }
    invariant_violation("verified call expression has no callable canonical callee type");
}

} // namespace

auto callable_contract(SemanticDraftView semantic, HIRTypeID verified_callable_type) noexcept
    -> std::optional<CallContractView> {
    return project_callable_contract(semantic, verified_callable_type);
}

auto call_contract(SemanticDraftView semantic, HIRExprID verified_call) noexcept
    -> CallContractView {
    return project_call_contract(semantic, verified_call);
}
