module carven:semantic.analysis.expr.text;

import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.semir.structured;
import :support.invariant;
import std;

template<typename Site>
auto construct_text_value(
    Site& site,
    TextIntrinsic intrinsic,
    TypeID type,
    typename Site::Value value,
    std::optional<ConstantID> known,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    auto state = Site::operand_state();
    auto operand = site.consume_read(state, std::move(value), span);
    if (!operand) {
        return std::unexpected(operand.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*operand)});
    return site.finish_constructed(
        type,
        SemTextIntrinsic {.intrinsic = intrinsic, .operands = std::move(operands)},
        std::move(state),
        span,
        known
    );
}

template<typename Site>
auto construct_text_call(
    Site& site,
    TextIntrinsic intrinsic,
    std::optional<typename Site::Value> receiver,
    std::span<const ASTCallArgument> arguments,
    Span span
) noexcept -> ExpressionTask<typename Site::Value> {
    if constexpr (Site::mode == ExpressionMode::RequiredRoot) {
        if (intrinsic != TextIntrinsic::New
            && intrinsic != TextIntrinsic::FromStr
            && intrinsic != TextIntrinsic::AsStr) {
            co_return std::unexpected(ExpressionNotAdmitted {});
        }
    }
    const auto contract = text_intrinsic_contract(intrinsic);
    auto state = Site::operand_state();
    auto operands = std::vector<SemCallArgument>();
    if (receiver) {
        const auto access = contract.parameters.front().access;
        auto value = [&]() noexcept -> ExpressionResult<SemanticExpression> {
            if constexpr (Site::mode == ExpressionMode::Body) {
                if (access == AccessMode::Write) {
                    return site.consume_write(state, std::move(*receiver), span);
                }
            }
            return site.consume_read(state, std::move(*receiver), span);
        }();
        if (!value) {
            co_return std::unexpected(value.error());
        }
        operands.push_back({.access = access, .expression = std::move(*value)});
    }
    for (const auto& argument : arguments) {
        const auto& parameter = contract.parameters[operands.size()];
        if (parameter.access != AccessMode::Read) {
            invariant_violation("text call explicit arguments require Read access");
        }
        const auto argument_type = resolve_text_intrinsic_type(site.draft(), parameter.type);
        const auto execution = site.enter_operand_execution(state.completes);
        auto built = (co_await site.read_argument(argument.expression, argument_type));
        if (!built) {
            co_return std::unexpected(built.error());
        }
        auto value = site.consume_read(
            state,
            std::move(*built),
            site.syntax().expression(argument.expression).span
        );
        if (!value) {
            co_return std::unexpected(value.error());
        }
        operands.push_back({.access = parameter.access, .expression = std::move(*value)});
    }
    co_return site.finish_constructed(
        resolve_text_intrinsic_type(site.draft(), contract.result),
        SemTextIntrinsic {.intrinsic = intrinsic, .operands = std::move(operands)},
        std::move(state),
        span
    );
}
