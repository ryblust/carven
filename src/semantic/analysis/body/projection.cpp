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
import :semantic.analysis.constant.fold;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.cpp.identifier;
import :support.invariant;
import :support.unique_indirect;
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
    auto formed = draft().require_type_formation(
        {TypeFormationRequirementKind::NonVoidObject,
         pointer->target,
         origin(source.operator_span)},
        generic_context ? std::optional(generic_context->definition) : std::nullopt
    );
    if (!formed) {
        co_return std::unexpected(formed.error());
    }
    auto pending = take_pending_failures(*operand);
    auto value = consume_value(*operand, span, AccessMode::Read);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    co_return BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {active_builder().make_place(
            std::nullopt,
            pointer->access == PointerAccess::Write ? AccessMode::Write : AccessMode::Read,
            pointer->target,
            SemDereference {.source = UniqueIndirect(std::move(*value)), .origin = origin(span)},
            origin(span)
        )}},
        .pending_failures = std::move(pending),
        .takeable = false,
        .completes = operand->completes,
    };
}

auto BodyExprSite::external_index(Value receiver, Value index, Span span) noexcept
    -> AnalysisResult<Value> {
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
    -> AnalysisResult<Selection> {
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
) noexcept -> AnalysisResult<Value> {
    const auto receiver_takeable = receiver.takeable;
    const auto receiver_shape = sequence_shape(draft(), receiver.type());
    const auto takeable =
        receiver.takeable && receiver_shape && receiver_shape->kind == SequenceShapeKind::Array;
    const auto known = element_constant(draft(), receiver.constant(), index.constant());
    auto state = operand_state();
    state.completes = receiver.completes;
    append_pending_failures(state.pending, take_pending_failures(receiver));
    auto subscript = consume_read(state, std::move(index), span);
    if (!subscript) {
        return std::unexpected(subscript.error());
    }
    if (auto* place = std::get_if<PlaceExpression>(&*receiver.storage); array && place != nullptr) {
        return Value {
            .storage = UniqueIndirect {BodyExpressionStorage {body.active_builder().make_place(
                place->root,
                place->access,
                type,
                SemIndex {
                    UniqueIndirect(std::move(place->expression)),
                    UniqueIndirect(std::move(*subscript)),
                    bounds,
                    true
                },
                body.origin(span)
            )}},
            .pending_failures = std::move(state.pending),
            .takeable = takeable,
            .completes = state.completes
        };
    }
    auto value = consume_read(state, std::move(receiver), span);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (!array) {
        // A slice value borrows its elements from another owner. Indexing still
        // denotes that element's place, but grants only Read access.
        auto place = body.active_builder().make_place(
            std::nullopt,
            AccessMode::Read,
            type,
            SemIndex {
                UniqueIndirect(std::move(*value)),
                UniqueIndirect(std::move(*subscript)),
                bounds,
                false
            },
            body.origin(span)
        );
        place.expression.constant = known;
        return Value {
            .storage = UniqueIndirect {BodyExpressionStorage {std::move(place)}},
            .pending_failures = std::move(state.pending),
            .takeable = false,
            .completes = state.completes
        };
    }
    auto projected = finish_constructed(
        type,
        SemIndex {
            UniqueIndirect(std::move(*value)),
            UniqueIndirect(std::move(*subscript)),
            bounds,
            true
        },
        std::move(state),
        span,
        known
    );
    if (projected) {
        projected->takeable = receiver_takeable;
    }
    return projected;
}

auto BodyExprSite::observe_field(
    ProgramOriginID definition,
    Span name,
    ConstructionTypeRef type
) noexcept -> void {
    if (body.observe_sources) {
        auto location = draft().source_span(definition);
        // Field origins cover the declaration for diagnostics. Navigation points
        // to the source name, including fields of instantiated generic records.
        for (const auto& tree : draft().syntax_trees()) {
            if (!tree) {
                continue;
            }
            const auto syntax = tree->view();
            if (syntax.source_id() != location.source_id) {
                continue;
            }
            for (const auto& item : syntax.items()) {
                const auto* record = std::get_if<ASTRecordDecl>(&item.value);
                if (record == nullptr) {
                    continue;
                }
                for (const auto& field : record->fields) {
                    if (field.span == location.span) {
                        location.span = field.name_span;
                        body.observe_source(name, location, type);
                        return;
                    }
                }
            }
            break;
        }
        body.observe_source(name, location, type);
    }
}

auto BodyExprSite::finish_field(
    ConstructionTypeRef type,
    FieldProjection field,
    Value receiver,
    Span span
) noexcept -> AnalysisResult<Value> {
    const auto takeable = receiver.takeable;
    const auto known = field_constant(draft(), receiver.constant(), field.field_index);
    if (auto* place = std::get_if<PlaceExpression>(&*receiver.storage)) {
        return Value {
            .storage = UniqueIndirect {BodyExpressionStorage {body.active_builder().make_place(
                place->root,
                place->access,
                type,
                SemField {UniqueIndirect(std::move(place->expression)), field},
                body.origin(span)
            )}},
            .pending_failures = take_pending_failures(receiver),
            .takeable = receiver.takeable,
            .completes = receiver.completes
        };
    }
    auto state = operand_state();
    auto value = consume_read(state, std::move(receiver), span);
    if (!value) {
        return std::unexpected(value.error());
    }
    auto projected = finish_constructed(
        type,
        SemField {UniqueIndirect(std::move(*value)), field},
        std::move(state),
        span,
        known
    );
    if (projected) {
        projected->takeable = takeable;
    }
    return projected;
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

auto BodyExprSite::finish_tuple_element(
    ConstructionTypeRef type,
    std::uint32_t index,
    Value receiver,
    Span span
) noexcept -> AnalysisResult<Value> {
    const auto takeable = receiver.takeable;
    auto known = std::optional<ConstantID>();
    if (const auto constant = receiver.constant()) {
        const auto* tuple = std::get_if<TupleConstant>(&draft().constant(*constant).value);
        if (tuple) {
            known = tuple->elements.at(index);
        }
    }
    if (auto* place = std::get_if<PlaceExpression>(&*receiver.storage)) {
        auto projected = body.active_builder().make_place(
            place->root,
            place->access,
            type,
            SemTupleElement {UniqueIndirect(std::move(place->expression)), index},
            body.origin(span)
        );
        projected.expression.constant = known;
        return Value {
            .storage = UniqueIndirect {BodyExpressionStorage {std::move(projected)}},
            .pending_failures = take_pending_failures(receiver),
            .takeable = takeable,
            .completes = receiver.completes
        };
    }
    auto state = operand_state();
    auto value = consume_read(state, std::move(receiver), span);
    if (!value) {
        return std::unexpected(value.error());
    }
    auto projected = finish_constructed(
        type,
        SemTupleElement {UniqueIndirect(std::move(*value)), index},
        std::move(state),
        span,
        known
    );
    if (projected) {
        projected->takeable = takeable;
    }
    return projected;
}
