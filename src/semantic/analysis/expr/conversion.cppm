module carven:semantic.analysis.expr.conversion;

import :semantic.analysis.expr.aggregate;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.expr.text;
import :semantic.analysis.operations;
import :semantic.semir.structured;
import std;

template<typename Site>
auto construct_cast(
    Site& site,
    CastKind kind,
    ConstructionTypeRef target,
    typename Site::Value value,
    std::optional<ConstantID> known,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    auto state = typename Site::OperandState();
    auto operand = site.consume_read(state, std::move(value), span);
    if (!operand) {
        return std::unexpected(operand.error());
    }
    return site.finish_constructed(
        target,
        SemCast {.operand = OwnedSemanticExpression(std::move(*operand)), .kind = kind},
        std::move(state),
        span,
        known
    );
}

// Shared language conversions. Native and callable/array adoption are completed
// by the body site after these operations decline the conversion.
template<typename Site>
auto convert_intrinsic_argument(
    Site& site,
    typename Site::Value& value,
    ConstructionTypeRef target,
    Span span
) noexcept -> ExpressionResult<bool> {
    const auto source = site.type(value);
    if (source == target) {
        return true;
    }
    if (source == ConstructionTypeRef(site.draft().intern_builtin_type(BuiltinType::String))
        && target == ConstructionTypeRef(site.draft().intern_builtin_type(BuiltinType::Str))) {
        auto converted = construct_text_value(
            site,
            TextIntrinsic::AsStr,
            site.draft().intern_builtin_type(BuiltinType::Str),
            std::move(value),
            std::nullopt,
            span
        );
        if (!converted) {
            return std::unexpected(converted.error());
        }
        value = std::move(*converted);
        return true;
    }
    if (const auto source_element = array_element(site.draft(), source)) {
        if (const auto target_element = slice_element(site.draft(), target)) {
            if (auto checked =
                    site.require_invariant_storage(*source_element, *target_element, span);
                !checked) {
                return std::unexpected(checked.error());
            }
            auto converted =
                construct_slice_call(site, SliceIntrinsic::FromArray, std::move(value), {}, span);
            if (!converted) {
                return std::unexpected(converted.error());
            }
            value = std::move(*converted);
            return true;
        }
    }
    if (pointer_narrows(site.draft(), source, target)) {
        auto known = std::optional<ConstantID>();
        if constexpr (Site::mode == ExpressionMode::Body) {
            if (site.known(value)) {
                known = site.draft().intern_constant(
                    {.type = std::get<TypeID>(target), .value = NullPointerConstant {}}
                );
            }
        }
        auto converted = ExpressionResult<typename Site::Value>(
            construct_cast(site, CastKind::PointerRead, target, std::move(value), known, span)
        );
        if (!converted) {
            return std::unexpected(converted.error());
        }
        value = std::move(*converted);
        return true;
    }
    return false;
}
