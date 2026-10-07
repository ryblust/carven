module carven:semantic.analysis.expr.numeric;

import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.semir.operation;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

template<typename Site>
auto construct_float_query(
    Site& site,
    FloatIntrinsic intrinsic,
    typename Site::Value receiver,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    auto state = Site::operand_state();
    auto operand = site.consume_read(state, std::move(receiver), span);
    if (!operand) {
        return std::unexpected(operand.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*operand)});
    return site.finish_constructed(
        site.draft().builtin_type(BuiltinType::Bool),
        SemIntrinsic {.operation = intrinsic, .operands = std::move(operands)},
        std::move(state),
        span
    );
}
