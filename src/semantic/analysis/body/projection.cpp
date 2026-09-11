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
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.cpp.identifier;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::dereference_expression(const ASTPrefixExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    auto operand = expression(source.operand_id);
    if (!operand) {
        return std::unexpected(operand.error());
    }
    const auto pointer = pointer_shape(draft(), operand->type());
    if (!pointer) {
        return std::unexpected(fail(
            source.operator_span,
            DiagnosticCode::TypeMismatch,
            "dereference requires a ptr value"
        ));
    }
    if (is_void_type(draft(), pointer->target)) {
        return std::unexpected(fail(
            source.operator_span,
            DiagnosticCode::TypeValueRequired,
            "ptr<void> has no object to dereference"
        ));
    }
    auto pending = take_pending_failures(*operand);
    auto value = consume_value(*operand, span, AccessMode::Read);
    if (!value) {
        return std::unexpected(value.error());
    }
    return BuiltExpression {
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

auto BodyElaborator::index_expression(const ASTIndexExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    auto operand = expression(source.operand_id);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    auto index = expression(source.index);
    if (!index.has_value()) {
        return std::unexpected(index.error());
    }
    auto pending_failures = take_pending_failures(*operand);
    append_pending_failures(pending_failures, take_pending_failures(*index));
    auto index_value = consume_value(*index, ast.expression(source.index).span, AccessMode::Read);
    if (!index_value.has_value()) {
        return std::unexpected(index_value.error());
    }
    if (is_cpp_type(operand->type())) {
        auto operands = std::vector<SemCallArgument>();
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*index_value)});
        operand->pending_failures = std::move(pending_failures);
        operand->completes = operand->completes && index->completes;
        return cpp_projection(std::move(*operand), CppIndexOperation {}, std::move(operands), span);
    }
    auto element = std::optional<ConstructionTypeRef>();
    auto extent = std::optional<std::uint64_t>();
    auto slice = false;
    if (const auto* concrete = std::get_if<TypeID>(&operand->type())) {
        const auto canonical = draft().type_copy(*concrete);
        if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
            element = array->element;
            extent = array->extent;
        } else if (const auto* view = std::get_if<SliceTypeValue>(&canonical.value)) {
            element = view->element;
            slice = true;
        }
    } else {
        const auto construction =
            draft().construction_type_copy(std::get<TypeTermID>(operand->type()));
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            element = array->element;
            extent = array->extent;
        } else if (const auto* view =
                       std::get_if<ConstructionSliceTypeValue>(&construction.value)) {
            element = view->element;
            slice = true;
        }
    }
    if (!element.has_value()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeNotIndexable,
            "indexing requires an array or slice value"
        ));
    }
    auto bounds = IndexBoundsPolicy {RuntimeCheckedBounds {}};
    if (extent && index_value->constant.has_value()) {
        const auto constant = draft().constant_copy(*index_value->constant);
        const auto* integer = std::get_if<IntegerConstant>(&constant.value);
        if (integer != nullptr) {
            if (integer->negative() || integer->magnitude() >= *extent) {
                return std::unexpected(fail(
                    ast.expression(source.index).span,
                    DiagnosticCode::ConstIndexBounds,
                    "constant array index is out of bounds"
                ));
            }
            bounds = ProvenInBounds {};
        }
    }
    if (auto* place = std::get_if<PlaceExpression>(&operand->storage); place != nullptr && !slice) {
        auto result = active_builder().make_place(
            place->root,
            *element,
            SemIndex {
                UniqueIndirect(std::move(place->expression)),
                UniqueIndirect(std::move(*index_value)),
                bounds
            },
            origin(span)
        );
        return BuiltExpression {
            .storage = std::move(result),

            .pending_failures = std::move(pending_failures),
            .completes = operand->completes && index->completes,
        };
    }
    auto source_value =
        consume_value(*operand, ast.expression(source.operand_id).span, AccessMode::Read);
    if (!source_value.has_value()) {
        return std::unexpected(source_value.error());
    }
    auto result = active_builder().make_expression(
        *element,
        active_builder().lifetime(),
        origin(span),
        SemIndex {
            UniqueIndirect(std::move(*source_value)),
            UniqueIndirect(std::move(*index_value)),
            bounds
        }
    );
    return BuiltExpression {
        .storage = std::move(result),

        .pending_failures = std::move(pending_failures),
        .completes = operand->completes && index->completes,
    };
}

auto BodyElaborator::select_member(
    const ASTMemberExpr& source,
    Span span,
    BuiltExpression operand
) noexcept -> AnalysisResult<SelectedExpression> {
    const auto name = spelling(source.name_span);
    if (is_cpp_type(operand.type())) {
        if (!is_supported_cpp_identifier(name)) {
            return std::unexpected(fail(
                source.name_span,
                DiagnosticCode::CppIdentifier,
                "external member cannot be represented as a C++ identifier"
            ));
        }
        return CppSelection {
            .target = CppMemberSelection {.receiver = std::move(operand), .member = name},
            .span = span
        };
    }
    auto pending_failures = take_pending_failures(operand);
    if (const auto* concrete = std::get_if<TypeID>(&operand.type())) {
        const auto canonical = draft().type_copy(*concrete);
        if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
            const auto declaration =
                draft().construction_struct_declaration_copy(structure->structure);
            const auto field = std::ranges::find(
                declaration.fields,
                name,
                [&](const ConstructionStructField& value) noexcept {
                    return draft().spelling_copy(value.name);
                }
            );
            if (field == declaration.fields.end()) {
                return std::unexpected(fail(
                    source.name_span,
                    DiagnosticCode::TypeMemberUnresolved,
                    std::format("structure has no field named '{}'", name)
                ));
            }
            const auto index =
                static_cast<std::uint32_t>(std::distance(declaration.fields.begin(), field));
            if (auto* place = std::get_if<PlaceExpression>(&operand.storage)) {
                auto result = active_builder().make_place(
                    place->root,
                    field->type,
                    SemField {
                        UniqueIndirect(std::move(place->expression)),
                        FieldProjection {.owner = structure->structure, .field_index = index}
                    },
                    origin(span)
                );
                return BuiltExpression {
                    .storage = std::move(result),

                    .pending_failures = std::move(pending_failures),
                    .completes = operand.completes,
                };
            }
            auto source_value =
                consume_value(operand, ast.expression(source.operand_id).span, AccessMode::Read);
            if (!source_value.has_value()) {
                return std::unexpected(source_value.error());
            }
            auto result = active_builder().make_expression(
                field->type,
                active_builder().lifetime(),
                origin(span),
                SemField {
                    UniqueIndirect(std::move(*source_value)),
                    FieldProjection {.owner = structure->structure, .field_index = index}
                }
            );
            return BuiltExpression {
                .storage = std::move(result),

                .pending_failures = std::move(pending_failures),
                .completes = operand.completes,
            };
        }
    }
    return std::unexpected(fail(
        source.name_span,
        DiagnosticCode::TypeMemberUnresolved,
        std::format("type has no member named '{}'", name)
    ));
}

auto BodyElaborator::propagation_expression(const ASTPropagationExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    auto operand = expression(source.operand_id);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    auto propagated = propagate_pending(*operand, span);
    if (!propagated.has_value()) {
        return std::unexpected(propagated.error());
    }
    const auto type = operand->type();
    auto result = make_built(type, SemPropagate {UniqueIndirect(take_built(*operand, span))}, span);
    result.completes = operand->completes;
    return result;
}
