module carven:semantic.analysis.expr.interpolation;

import :frontend.ast.expr;
import :frontend.ast.ids;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.semir.format;
import :semantic.semir.structured;
import std;

struct NormalizedInterpolation final {
    FormatSpec specification;
    std::vector<ASTExprID> operands;
};

// Operands follow each outer hole with its specification holes in source order.
auto normalize_interpolation(const ASTInterpolationExpr& source) noexcept
    -> NormalizedInterpolation;

template<typename Site>
auto construct_interpolation(
    Site& site,
    const ASTInterpolationExpr& source,
    Span span,
    std::optional<typename Site::Value> receiver = std::nullopt
) noexcept -> ExpressionResult<typename Site::Value> {
    auto state = typename Site::OperandState();
    auto destination = std::optional<OwnedSemanticExpression>();
    if (receiver) {
        if constexpr (Site::mode == ExpressionMode::RequiredRoot) {
            return std::unexpected(ExpressionNotAdmitted {});
        } else {
            auto place = site.consume_write(state, std::move(*receiver), span);
            if (!place) {
                return std::unexpected(place.error());
            }
            destination = OwnedSemanticExpression(std::move(*place));
        }
    }
    auto normalized = normalize_interpolation(source);
    auto operands = std::vector<SemCallArgument>();
    operands.reserve(normalized.operands.size());
    for (const auto expression : normalized.operands) {
        const auto execution = site.enter_operand_execution(state.completes);
        auto built = site.read_argument(expression, std::nullopt);
        if (!built) {
            return std::unexpected(built.error());
        }
        auto operand =
            site.consume_read(state, std::move(*built), site.syntax().expression(expression).span);
        if (!operand) {
            return std::unexpected(operand.error());
        }
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*operand)});
    }
    return site.finish_constructed(
        site.draft().intern_builtin_type(receiver ? BuiltinType::Void : BuiltinType::String),
        SemFormat {
            .specification = std::move(normalized.specification),
            .operands = std::move(operands),
            .receiver = std::move(destination)
        },
        std::move(state),
        span
    );
}
