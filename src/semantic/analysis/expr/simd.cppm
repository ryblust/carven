module carven:semantic.analysis.expr.simd;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.result;
import :semantic.semir.constant;
import :semantic.semir.simd;
import :semantic.semir.structured;
import std;

template<typename Site>
auto construct_simd_call(
    Site& site,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::optional<typename Site::Value> receiver,
    std::span<const ASTCallArgument> arguments,
    Span span
) noexcept -> ExpressionTask<typename Site::Value> {
    const auto contract = simd_contract(intrinsic, owner);
    if (arguments.size() + (receiver ? 1uz : 0uz) != contract.inputs.size()) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeMethodCallArity,
            "SIMD operation argument count does not match"
        ));
    }
    auto state = Site::operand_state();
    auto operands = std::vector<SemCallArgument>();
    if (receiver) {
        auto value = site.consume_read(state, std::move(*receiver), span);
        if (!value) {
            co_return std::unexpected(value.error());
        }
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
    }
    for (const auto& argument : arguments) {
        const auto type = resolve_simd_type(site.draft(), contract.inputs[operands.size()]);
        const auto execution = site.enter_operand_execution(state.completes);
        auto built = contract.stages[operands.size()] == ParameterStage::Static
            ? co_await site.read_static_argument(argument.expression, type)
            : co_await site.read_argument(argument.expression, type);
        if (!built) {
            co_return std::unexpected(built.error());
        }
        auto converted =
            site.convert_argument(*built, type, site.syntax().expression(argument.expression).span);
        if (!converted) {
            co_return std::unexpected(converted.error());
        }
        auto value = site.consume_read(state, std::move(*built), span);
        if (!value) {
            co_return std::unexpected(value.error());
        }
        if (contract.stages[operands.size()] == ParameterStage::Static) {
            if (value->constant) {
                const auto& fact = site.draft().constant(*value->constant);
                const auto* control = std::get_if<IntegerConstant>(&fact.value);
                if (!control
                    || control->negative()
                    || control->magnitude() >= simd_static_limit(intrinsic, owner)) {
                    co_return std::unexpected(site.fail(
                        span,
                        DiagnosticCode::ConstIndexBounds,
                        "SIMD immediate control is out of range"
                    ));
                }
            }
        }
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
    }
    co_return site.finish_constructed(
        resolve_simd_type(site.draft(), contract.result),
        SemIntrinsic {.operation = intrinsic, .operands = std::move(operands)},
        std::move(state),
        span
    );
}
