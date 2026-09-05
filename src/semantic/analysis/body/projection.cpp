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
import :semantic.analysis.constant.proof;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {

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
    auto pending_failures = take_pending(*operand);
    append_pending(pending_failures, take_pending(*index));
    auto index_value = as_value(*index, ast.expression(source.index).span, AccessMode::Read);
    if (!index_value.has_value()) {
        return std::unexpected(index_value.error());
    }
    auto element = std::optional<ConstructionTypeRef>();
    auto extent = std::optional<std::uint64_t>();
    if (const auto* concrete = std::get_if<TypeID>(&operand->type)) {
        const auto canonical = draft().type_copy(*concrete);
        if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
            element = array->element;
            extent = array->extent;
        }
    } else {
        const auto construction =
            draft().construction_type_copy(std::get<TypeTermID>(operand->type));
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            element = array->element;
            extent = array->extent;
        }
    }
    if (!element.has_value() || !extent.has_value()) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeNotIndexable, "indexing requires an array value")
        );
    }
    auto bounds = ArrayBoundsPolicy {RuntimeCheckedBounds {}};
    if (index->constant.has_value()) {
        const auto constant = draft().constant_copy(*index->constant);
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
    if (const auto* place = std::get_if<PlaceHandle>(&operand->storage)) {
        const auto result = active_builder().append_place(
            *place,
            *element,
            expression_construction::IndexProjection {.index = *index_value, .bounds = bounds},
            origin(span)
        );
        return BuiltExpression {
            .type = *element,
            .storage = result,
            .constant = std::nullopt,
            .pending_failures = std::move(pending_failures),
            .completes = operand->completes && index->completes,
        };
    }
    auto source_value =
        as_value(*operand, ast.expression(source.operand_id).span, AccessMode::Read);
    if (!source_value.has_value()) {
        return std::unexpected(source_value.error());
    }
    const auto result = active_builder().append_value(
        *element,
        active_builder().lifetime(),
        expression_construction::Project {
            .source = *source_value,
            .projection =
                expression_construction::IndexProjection {
                    .index = *index_value,
                    .bounds = bounds,
                },
        },
        origin(span)
    );
    return BuiltExpression {
        .type = *element,
        .storage = result,
        .constant = std::nullopt,
        .pending_failures = std::move(pending_failures),
        .completes = operand->completes && index->completes,
    };
}

auto BodyElaborator::member_expression(const ASTMemberExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    if (source.op == ASTMemberOperator::Scope) {
        auto enumeration = resolve_enum_qualifier(source.operand_id);
        if (!enumeration.has_value()) {
            return std::unexpected(enumeration.error());
        }
        if (!enumeration->has_value()) {
            return std::unexpected(fail(
                ast.expression(source.operand_id).span,
                DiagnosticCode::TypeEnumContext,
                "enum case qualifier does not name an enum type"
            ));
        }
        return enum_case_reference(
            **enumeration,
            spelling(source.name_span),
            span,
            source.name_span
        );
    }
    auto operand = expression(source.operand_id);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    return member_projection(source, span, std::move(*operand));
}

auto BodyElaborator::member_projection(
    const ASTMemberExpr& source,
    Span span,
    BuiltExpression operand
) noexcept -> AnalysisResult<BuiltExpression> {
    const auto name = spelling(source.name_span);
    auto pending_failures = take_pending(operand);
    if (const auto* concrete = std::get_if<TypeID>(&operand.type)) {
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
            if (const auto* place = std::get_if<PlaceHandle>(&operand.storage)) {
                const auto result = active_builder().append_place(
                    *place,
                    field->type,
                    FieldProjection {.owner = structure->structure, .field_index = index},
                    origin(span)
                );
                return BuiltExpression {
                    .type = field->type,
                    .storage = result,
                    .constant = std::nullopt,
                    .pending_failures = std::move(pending_failures),
                    .completes = operand.completes,
                };
            }
            auto source_value =
                as_value(operand, ast.expression(source.operand_id).span, AccessMode::Read);
            if (!source_value.has_value()) {
                return std::unexpected(source_value.error());
            }
            const auto result = active_builder().append_value(
                field->type,
                active_builder().lifetime(),
                expression_construction::Project {
                    .source = *source_value,
                    .projection =
                        FieldProjection {
                            .owner = structure->structure,
                            .field_index = index,
                        },
                },
                origin(span)
            );
            return BuiltExpression {
                .type = field->type,
                .storage = result,
                .constant = std::nullopt,
                .pending_failures = std::move(pending_failures),
                .completes = operand.completes,
            };
        }
        if (canonical.value == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Str}}) {
            auto decision = decide_text_property(name);
            if (!decision.has_value()) {
                return std::unexpected(fail(
                    source.name_span,
                    decision.error().code,
                    std::string(decision.error().message)
                ));
            }
            const auto result_type = draft().intern_builtin_type(text_intrinsic_result(*decision));
            auto folded =
                fold_text_intrinsic_constant(draft(), *decision, operand.constant, result_type);
            if (folded.has_value()) {
                auto result = publish_constant(std::move(*folded), span);
                result.pending_failures = std::move(pending_failures);
                return result;
            }
            if (const auto diagnostic = constant_evaluation_diagnostic(folded.error())) {
                return std::unexpected(
                    fail(source.name_span, diagnostic->code, std::string(diagnostic->message))
                );
            }
            auto source_value =
                as_value(operand, ast.expression(source.operand_id).span, AccessMode::Read);
            if (!source_value.has_value()) {
                return std::unexpected(source_value.error());
            }
            const auto result = active_builder().append_value(
                result_type,
                active_builder().lifetime(),
                expression_construction::TextOperation {
                    .source = *source_value,
                    .intrinsic = *decision
                },
                origin(span)
            );
            return BuiltExpression {
                .type = result_type,
                .storage = result,
                .constant = std::nullopt,
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
    const auto type = operand->type;
    auto result = make_built(
        type,
        SemPropagate<ConstructionTypeRef, FailureTermID> {
            UniqueIndirect(take_built(*operand, span))
        },
        span
    );
    result.completes = operand->completes;
    return result;
}


} // namespace body_elaboration
