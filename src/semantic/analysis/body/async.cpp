module carven:semantic.analysis.body.async.impl;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.tree;
import :semantic.analysis.body.context;
import :semantic.analysis.catalog;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.async;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.standard;
import :support.invariant;
import :support.unique_indirect;
import std;

// Initial source operations retain exact delayed failure terms until canonicalization.
auto BodyElaborator::operation_shape(ConstructionTypeRef type) const noexcept
    -> std::optional<ConstructionOperationTypeValue> {
    return ::operation_shape(draft(), type);
}

auto BodyElaborator::await_child_intrinsic(
    const ASTCallExpr& source,
    AsyncIntrinsic kind,
    Span span
) noexcept -> AnalysisTask<BuiltExpression> {
    const auto closing = kind == AsyncIntrinsic::CloseChild;
    if ((closing && source.arguments.size() != 1uz) || (!closing && source.arguments.empty())) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeCallArity,
            closing ? "close requires exactly one lexical child"
                    : "wait_ready requires at least one lexical child"
        ));
    }
    auto children = std::vector<LocalBindingID>();
    for (const auto& argument : source.arguments) {
        const auto& argument_source = ast.expression(argument);
        if (!std::holds_alternative<ASTNameExpr>(argument_source.value)) {
            co_return std::unexpected(fail(
                argument_source.span,
                DiagnosticCode::AsyncOwnership,
                "child waiting and closing require named lexical children"
            ));
        }
        auto selected = co_await expression(argument, true);
        if (!selected) {
            co_return std::unexpected(selected.error());
        }
        const auto* binding = std::get_if<SemBinding>(&selected->expression().value);
        if (binding == nullptr
            || binding->binding.owner() != active_builder().identity()
            || !async_child_bindings.contains(binding->binding)) {
            co_return std::unexpected(fail(
                argument_source.span,
                DiagnosticCode::AsyncOwnership,
                "child waiting and closing require a lexical child in the current activation"
            ));
        }
        if (std::ranges::contains(children, binding->binding)) {
            co_return std::unexpected(fail(
                argument_source.span,
                DiagnosticCode::AsyncOwnership,
                "wait_ready cannot select the same lexical child twice"
            ));
        }
        children.push_back(binding->binding);
    }
    if (closing) {
        co_return make_built(
            ConstructionTypeRef(draft().builtin_type(BuiltinType::Void)),
            SemCloseChild {.child = children.front()},
            span
        );
    }
    const auto type = draft().intern_type(
        CanonicalType {
            .value = TupleTypeValue {
                .elements = {
                    draft().builtin_type(BuiltinType::Usize),
                    draft().builtin_type(BuiltinType::Bool)
                }
            }
        }
    );
    co_return make_built(
        ConstructionTypeRef(type),
        SemWaitReady {.children = std::move(children)},
        span
    );
}

auto BodyElaborator::await_expression(const ASTPrefixExpr& source, Span span) noexcept
    -> AnalysisTask<BuiltExpression> {
    if (!async_body && !static_stage()) {
        co_return std::unexpected(fail(
            source.operator_span,
            DiagnosticCode::AsyncAdmission,
            "await requires an async fn body, async test, or const execution"
        ));
    }
    auto operand_id = source.operand_id;
    while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(operand_id).value)) {
        operand_id = group->expression;
    }
    if (const auto* call = std::get_if<ASTCallExpr>(&ast.expression(operand_id).value)) {
        auto callee_id = call->callee;
        while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(callee_id).value)) {
            callee_id = group->expression;
        }
        if (const auto* name = std::get_if<ASTNameExpr>(&ast.expression(callee_id).value)) {
            const auto text = spelling(name->name_span);
            const auto candidates = catalog().lookup(source_module_id, text);
            const auto* provided = candidates.size() == 1uz
                ? std::get_if<ProvidedExport>(&candidates.front().entity)
                : nullptr;
            if (find_local(text) == nullptr
                && provided
                && (*provided == ProvidedExport::WaitReady || *provided == ProvidedExport::Close)) {
                auto callee = co_await select_expression(call->callee, true);
                if (!callee) {
                    co_return std::unexpected(callee.error());
                }
                const auto* intrinsic = std::get_if<AsyncIntrinsicSelection>(&*callee);
                if (intrinsic == nullptr) {
                    invariant_violation("selected child observer lost its intrinsic identity");
                }
                co_return co_await await_child_intrinsic(*call, intrinsic->kind, span);
            }
        }
    }
    auto operand = co_await expression(source.operand_id, true);
    if (!operand) {
        co_return std::unexpected(operand.error());
    }
    const auto operation = operation_shape(operand->type());
    if (!operation) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::AsyncAdmission,
            "await requires a complete cold operation owner or lexical async child"
        ));
    }
    const auto* binding = std::get_if<SemBinding>(&operand->expression().value);
    const auto child = binding && async_child_bindings.contains(binding->binding);
    auto pending = take_pending_failures(*operand);
    auto value = child
        ? consume_place(*operand, span).transform([](PlaceExpression&& place) static noexcept {
              return std::move(place.expression);
          })
        : consume_value(*operand, span, AccessMode::Take);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    pending.push_back(operation->failures);
    auto result = make_built(
        operation->success,
        SemAwait {
            .operand = UniqueIndirect(std::move(*value)),
            .operand_kind =
                child ? AsyncAwaitOperandKind::LexicalChild : AsyncAwaitOperandKind::ColdOperation
        },
        span,
        std::move(pending)
    );
    result.completes = operand->completes;
    co_return result;
}
