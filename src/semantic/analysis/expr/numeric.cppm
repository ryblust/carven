module carven:semantic.analysis.expr.numeric;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.expr.simd;
import :semantic.semir.operation;
import :semantic.semir.simd;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

template<typename Site>
auto construct_float_call(
    Site& site,
    FloatIntrinsic intrinsic,
    typename Site::Value receiver,
    std::span<const ASTExprID> arguments,
    Span span
) noexcept -> ExpressionTask<typename Site::Value> {
    auto state = Site::operand_state();
    auto operand = site.consume_read(state, std::move(receiver), span);
    if (!operand) {
        co_return std::unexpected(operand.error());
    }
    const auto receiver_type = operand->type.construction();
    const auto owner =
        std::get<BuiltinTypeValue>(site.draft().type_copy(std::get<TypeID>(receiver_type)).value)
            .kind;
    const auto layout = simd_layout(owner);
    const auto result_type = intrinsic == FloatIntrinsic::IsFinite
        ? ConstructionTypeRef(site.draft().builtin_type(layout ? layout->mask : BuiltinType::Bool))
        : receiver_type;
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*operand)});
    for (const auto source : arguments) {
        const auto argument_span = site.syntax().expression(source).span;
        const auto execution = site.enter_operand_execution(state.completes);
        const auto selected = call_argument_operand(site.syntax(), source);
        if (selected.access != AccessMode::Read) {
            co_return std::unexpected(site.fail(
                argument_span,
                DiagnosticCode::AccessCallMismatch,
                "argument access marker differs from the parameter"
            ));
        }
        const auto argument_type = layout
            ? ConstructionTypeRef(site.draft().builtin_type(layout->element))
            : receiver_type;
        auto argument = co_await site.read(selected.expression, argument_type);
        if (!argument) {
            co_return std::unexpected(argument.error());
        }
        if (layout && site.type(*argument) == argument_type) {
            auto broadcast =
                construct_simd_splat(site, *layout, std::move(*argument), argument_span);
            if (!broadcast) {
                co_return std::unexpected(broadcast.error());
            }
            argument = std::move(*broadcast);
        } else if (layout && site.type(*argument) != receiver_type) {
            co_return std::unexpected(site.fail(
                argument_span,
                DiagnosticCode::TypeMismatch,
                "floating SIMD argument requires the same vector type or f32"
            ));
        }
        auto converted = site.convert_argument(*argument, receiver_type, argument_span);
        if (!converted) {
            co_return std::unexpected(converted.error());
        }
        auto value = site.consume_read(state, std::move(*argument), argument_span);
        if (!value) {
            co_return std::unexpected(value.error());
        }
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
    }
    co_return site.finish_constructed(
        result_type,
        SemIntrinsic {.operation = intrinsic, .operands = std::move(operands)},
        std::move(state),
        span
    );
}
