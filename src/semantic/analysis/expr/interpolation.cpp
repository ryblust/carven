module carven:semantic.analysis.expr.interpolation.impl;

import :frontend.ast.expr;
import :semantic.analysis.expr.interpolation;
import :semantic.semir.format;
import std;

auto normalize_interpolation(const ASTInterpolationExpr& source) noexcept
    -> NormalizedInterpolation {
    auto result = NormalizedInterpolation {.specification = {}, .operands = {}};
    const auto build =
        [&](this const auto& self,
            std::span<const ASTInterpolationPart> parts) noexcept -> std::vector<FormatPart> {
        auto target = std::vector<FormatPart>();
        for (const auto& part : parts) {
            if (const auto* text = std::get_if<ASTInterpolationText>(&part.value)) {
                target.push_back({FormatText {.bytes = text->bytes}});
            } else {
                const auto& hole = std::get<ASTInterpolationHole>(part.value);
                const auto index = result.operands.size();
                result.operands.push_back(hole.expression);
                target.push_back({FormatHole {
                    .operand_index = index,
                    .has_specification = hole.colon_span.has_value(),
                    .specification = self(hole.specification),
                }});
            }
        }
        return target;
    };
    result.specification.parts = build(source.parts);
    return result;
}
