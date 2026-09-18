module carven:semantic.analysis.body.projection.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.body.resolve;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.cpp.identifier;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::dereference_expression(const ASTPrefixExpr& source, Span span) noexcept
    -> AnalysisTask<BuiltExpression> {
    auto operand = (co_await expression(source.operand_id));
    if (!operand) {
        co_return std::unexpected(operand.error());
    }
    const auto pointer = pointer_shape(draft(), operand->type());
    if (!pointer) {
        co_return std::unexpected(fail(
            source.operator_span,
            DiagnosticCode::TypeMismatch,
            "dereference requires a ptr value"
        ));
    }
    if (is_void_type(draft(), pointer->target)) {
        co_return std::unexpected(fail(
            source.operator_span,
            DiagnosticCode::TypeValueRequired,
            "ptr<void> has no object to dereference"
        ));
    }
    auto pending = take_pending_failures(*operand);
    auto value = consume_value(*operand, span, AccessMode::Read);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    co_return BuiltExpression {
        .storage = active_builder().make_place(
            std::nullopt,
            pointer->target,
            SemDereference {.source = UniqueIndirect(std::move(*value)), .origin = origin(span)},
            origin(span)
        ),
        .pending_failures = std::move(pending),
        .takeable = false,
        .completes = operand->completes,
    };
}

auto BodyExprSite::external_index(Value receiver, Value index, Span span) noexcept
    -> ExpressionResult<Value> {
    auto state = operand_state();
    auto subscript = consume_read(state, std::move(index), span);
    if (!subscript) {
        return std::unexpected(subscript.error());
    }
    append_pending_failures(receiver.pending_failures, state.pending);
    receiver.completes &= state.completes;
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*subscript)});
    return body
        .cpp_projection(std::move(receiver), CppIndexOperation {}, std::move(operands), span);
}

auto BodyExprSite::external_member(const ASTMemberExpr& source, Value receiver, Span span) noexcept
    -> ExpressionResult<Selection> {
    const auto name = spelling(source.name_span);
    if (!is_supported_cpp_identifier(name)) {
        return std::unexpected(fail(
            source.name_span,
            DiagnosticCode::CppIdentifier,
            "external member cannot be represented as a C++ identifier"
        ));
    }
    return CppSelection {
        .target = CppMemberSelection {.receiver = std::move(receiver), .member = name},
        .span = span
    };
}

auto BodyExprSite::finish_index(
    ConstructionTypeRef type,
    bool array,
    IndexBoundsPolicy bounds,
    Value receiver,
    Value index,
    Span span
) noexcept -> ExpressionResult<Value> {
    auto state = operand_state();
    state.completes = receiver.completes;
    append_pending_failures(state.pending, take_pending_failures(receiver));
    auto subscript = consume_read(state, std::move(index), span);
    if (!subscript) {
        return std::unexpected(subscript.error());
    }
    if (auto* place = std::get_if<PlaceExpression>(&receiver.storage); array && place != nullptr) {
        return Value {
            .storage = body.active_builder().make_place(
                place->root,
                type,
                SemIndex {
                    UniqueIndirect(std::move(place->expression)),
                    UniqueIndirect(std::move(*subscript)),
                    bounds
                },
                body.origin(span)
            ),
            .pending_failures = std::move(state.pending),
            .takeable = true,
            .completes = state.completes
        };
    }
    auto value = consume_read(state, std::move(receiver), span);
    if (!value) {
        return std::unexpected(value.error());
    }
    return finish_constructed(
        type,
        SemIndex {UniqueIndirect(std::move(*value)), UniqueIndirect(std::move(*subscript)), bounds},
        std::move(state),
        span
    );
}

auto BodyExprSite::finish_field(
    ConstructionTypeRef type,
    FieldProjection field,
    Value receiver,
    Span span
) noexcept -> ExpressionResult<Value> {
    if (auto* place = std::get_if<PlaceExpression>(&receiver.storage)) {
        return Value {
            .storage = body.active_builder().make_place(
                place->root,
                type,
                SemField {UniqueIndirect(std::move(place->expression)), field},
                body.origin(span)
            ),
            .pending_failures = take_pending_failures(receiver),
            .takeable = true,
            .completes = receiver.completes
        };
    }
    auto state = operand_state();
    auto value = consume_read(state, std::move(receiver), span);
    if (!value) {
        return std::unexpected(value.error());
    }
    return finish_constructed(
        type,
        SemField {UniqueIndirect(std::move(*value)), field},
        std::move(state),
        span
    );
}

auto BodyElaborator::propagation_expression(const ASTPropagationExpr& source, Span span) noexcept
    -> AnalysisTask<BuiltExpression> {
    auto operand = (co_await expression(source.operand_id));
    if (!operand.has_value()) {
        co_return std::unexpected(operand.error());
    }
    auto propagated = propagate_pending(*operand, span);
    if (!propagated.has_value()) {
        co_return std::unexpected(propagated.error());
    }
    const auto type = operand->type();
    auto result = make_built(type, SemPropagate {UniqueIndirect(take_built(*operand, span))}, span);
    result.completes = operand->completes;
    co_return result;
}
