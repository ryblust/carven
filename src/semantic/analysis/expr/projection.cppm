module carven:semantic.analysis.expr.projection;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.structured;
import std;

template<typename Site>
auto construct_index_expression(Site& site, const ASTIndexExpr& source, Span span) noexcept
    -> ExpressionResult<typename Site::Value> {
    auto receiver =
        ExpressionResult<typename Site::Value>(site.read(source.operand_id, std::nullopt));
    if (!receiver && std::holds_alternative<AnalysisFailure>(receiver.error())) {
        return std::unexpected(receiver.error());
    }
    // The index is independently checked even when its receiver is not admitted.
    // Type and bounds rules below require both operands to have been constructed.
    auto index = site.read(source.index, std::nullopt);
    if (!index) {
        return std::unexpected(index.error());
    }
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    if (site.external(site.type(*receiver))) {
        return site.external_index(std::move(*receiver), std::move(*index), span);
    }
    const auto shape = sequence_shape(site.draft(), site.type(*receiver));
    if (!shape) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeNotIndexable,
            "indexing requires an array or slice value"
        ));
    }
    const auto index_type = site.type(*index);
    const auto* concrete = std::get_if<TypeID>(&index_type);
    const auto canonical =
        concrete ? std::optional(site.draft().type_copy(*concrete)) : std::nullopt;
    const auto* builtin = canonical ? std::get_if<BuiltinTypeValue>(&canonical->value) : nullptr;
    if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
        return std::unexpected(site.fail(
            site.syntax().expression(source.index).span,
            DiagnosticCode::TypeIndexInteger,
            "array index requires an integer"
        ));
    }
    auto bounds = IndexBoundsPolicy {RuntimeCheckedBounds {}};
    if constexpr (Site::mode == ExpressionMode::Body) {
        if (const auto known = site.known(*index); known && shape->extent) {
            const auto& fact = site.draft().constant(*known);
            if (const auto* integer = std::get_if<IntegerConstant>(&fact.value)) {
                if (integer->negative() || integer->magnitude() >= *shape->extent) {
                    return std::unexpected(site.fail(
                        site.syntax().expression(source.index).span,
                        DiagnosticCode::ConstIndexBounds,
                        "constant array index is out of bounds"
                    ));
                }
                bounds = ProvenInBounds {};
            }
        }
    }
    return site.finish_index(
        shape->element,
        shape->extent.has_value(),
        bounds,
        std::move(*receiver),
        std::move(*index),
        span
    );
}

template<typename Site>
auto construct_member_expression(
    Site& site,
    const ASTMemberExpr& source,
    typename Site::Value receiver,
    Span span
) noexcept -> ExpressionResult<typename Site::Selection> {
    const auto name = site.spelling(source.name_span);
    if (site.external(site.type(receiver))) {
        return site.external_member(source, std::move(receiver), span);
    }
    const auto type = site.type(receiver);
    const auto* concrete = std::get_if<TypeID>(&type);
    const auto canonical =
        concrete ? std::optional(site.draft().type_copy(*concrete)) : std::nullopt;
    const auto* structure = canonical ? std::get_if<StructTypeValue>(&canonical->value) : nullptr;
    if (structure != nullptr) {
        const auto declaration =
            site.draft().construction_struct_declaration_copy(structure->structure);
        const auto field =
            std::ranges::find_if(declaration.fields, [&](const auto& field) noexcept {
                return site.draft().spelling(field.name) == name;
            });
        if (field == declaration.fields.end()) {
            return std::unexpected(site.fail(
                source.name_span,
                DiagnosticCode::TypeMemberUnresolved,
                std::format("structure has no field named '{}'", name)
            ));
        }
        const auto index = static_cast<std::uint32_t>(field - declaration.fields.begin());
        return site.finish_field(
            field->type,
            FieldProjection {.owner = structure->structure, .field_index = index},
            std::move(receiver),
            span
        );
    }
    return std::unexpected(site.fail(
        source.name_span,
        DiagnosticCode::TypeMemberUnresolved,
        std::format("type has no member named '{}'", name)
    ));
}
