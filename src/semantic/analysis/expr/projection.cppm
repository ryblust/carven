module carven:semantic.analysis.expr.projection;

import :diagnostics.code;
import :diagnostics.suggestion;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.analysis.types.display;
import :semantic.semir.structured;
import std;

template<typename Site>
auto construct_tuple_element_expression(
    Site& site,
    const ASTTupleElementExpr& source,
    Span span
) noexcept -> ExpressionTask<typename Site::Value> {
    auto receiver = co_await site.read(source.operand_id, std::nullopt);
    if (!receiver) {
        co_return std::unexpected(receiver.error());
    }
    const auto elements = tuple_elements(site.draft(), site.type(*receiver));
    if (!elements) {
        co_return std::unexpected(site.fail(
            source.index_span,
            DiagnosticCode::TypeMemberUnresolved,
            "positional field access requires a tuple value"
        ));
    }
    if (source.index >= elements->size()) {
        co_return std::unexpected(site.fail(
            source.index_span,
            DiagnosticCode::ConstIndexBounds,
            "tuple field position is out of bounds"
        ));
    }
    co_return site
        .finish_tuple_element((*elements)[source.index], source.index, std::move(*receiver), span);
}

template<typename Site>
auto construct_index_expression(Site& site, const ASTIndexExpr& source, Span span) noexcept
    -> ExpressionTask<typename Site::Value> {
    auto receiver = ExpressionResult<typename Site::Value>(
        (co_await site.read(source.operand_id, std::nullopt))
    );
    if (!receiver && std::holds_alternative<AnalysisFailure>(receiver.error())) {
        co_return std::unexpected(receiver.error());
    }
    // The index is independently checked even when its receiver is not admitted.
    // Type and bounds rules below require both operands to have been constructed.
    auto index = (co_await site.read(source.index, std::nullopt));
    if (!index) {
        co_return std::unexpected(index.error());
    }
    if (!receiver) {
        co_return std::unexpected(receiver.error());
    }
    if (site.external(site.type(*receiver))) {
        co_return site.external_index(std::move(*receiver), std::move(*index), span);
    }
    const auto shape = sequence_shape(site.draft(), site.type(*receiver));
    if (!shape) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeNotIndexable,
            "indexing requires an array, slice, or Vector value"
        ));
    }
    const auto index_type = site.type(*index);
    const auto* concrete = std::get_if<TypeID>(&index_type);
    const auto canonical =
        concrete ? std::optional(site.draft().type_copy(*concrete)) : std::nullopt;
    const auto* builtin = canonical ? std::get_if<BuiltinTypeValue>(&canonical->value) : nullptr;
    if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
        co_return std::unexpected(site.fail(
            site.syntax().expression(source.index).span,
            DiagnosticCode::TypeIndexInteger,
            "array index requires an integer"
        ));
    }
    auto bounds = IndexBoundsPolicy {RuntimeCheckedBounds {}};
    if constexpr (Site::mode == ExpressionMode::Body) {
        if (const auto in_bounds = constant_index_in_bounds(
                site.draft(),
                site.known_sequence_extent(*receiver),
                site.known(*index)
            )) {
            if (!*in_bounds && shape->kind == SequenceShapeKind::Array) {
                co_return std::unexpected(site.fail(
                    site.syntax().expression(source.index).span,
                    DiagnosticCode::ConstIndexBounds,
                    "constant array index is out of bounds"
                ));
            }
            if (*in_bounds) {
                bounds = ProvenInBounds {};
            }
        }
    }
    co_return site.finish_index(
        shape->element,
        shape->kind != SequenceShapeKind::Slice,
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
    const auto structure = record_shape(site.draft(), type);
    if (structure) {
        if (const auto* concrete = std::get_if<StructID>(&structure->owner)) {
            if (auto access = site.representation_access(*concrete, source.name_span); !access) {
                return std::unexpected(access.error());
            }
        }
        const auto& declaration = *structure;
        const auto field =
            std::ranges::find_if(declaration.fields, [&](const auto& field) noexcept {
                return site.draft().spelling(field.name) == name;
            });
        if (field == declaration.fields.end()) {
            auto fields = std::vector<std::string>();
            for (const auto& declared : declaration.fields) {
                fields.push_back(site.draft().spelling_copy(declared.name));
            }
            return std::unexpected(site.fail(
                source.name_span,
                DiagnosticCode::TypeMemberUnresolved,
                std::format(
                    "structure '{}' has no field named '{}'{}",
                    site.draft().spelling(declaration.name),
                    name,
                    spelling_suggestion(name, fields)
                )
            ));
        }
        const auto index = static_cast<std::uint32_t>(field - declaration.fields.begin());
        const auto projection = FieldProjection {.owner = structure->owner, .field_index = index};
        site.observe_field(field->origin, source.name_span, field->type);
        return site.finish_field(field->type, projection, std::move(receiver), span);
    }
    return std::unexpected(site.fail(
        source.name_span,
        DiagnosticCode::TypeMemberUnresolved,
        std::format(
            "type '{}' has no member named '{}'",
            type_display_name(site.draft(), type),
            name
        )
    ));
}
