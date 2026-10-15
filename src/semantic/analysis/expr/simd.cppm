module carven:semantic.analysis.expr.simd;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.result;
import :semantic.analysis.operations;
import :semantic.semir.constant;
import :semantic.semir.simd;
import :semantic.semir.structured;
import std;

template<typename Site>
auto construct_simd_splat(
    Site& site,
    SIMDLayout layout,
    typename Site::Value input,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    auto state = Site::operand_state();
    auto scalar = site.consume_read(state, std::move(input), span);
    if (!scalar) {
        return std::unexpected(scalar.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*scalar)});
    return site.finish_constructed(
        site.draft().builtin_type(layout.vector),
        SemIntrinsic {.operation = SIMDIntrinsic::Splat, .operands = std::move(operands)},
        std::move(state),
        span
    );
}

template<typename Site>
auto construct_simd_call(
    Site& site,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::optional<typename Site::Value> receiver,
    std::span<const ASTExprID> arguments,
    Span span
) noexcept -> ExpressionTask<typename Site::Value> {
    auto contract = simd_contract(intrinsic, owner);
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
        auto type = resolve_simd_type(site.draft(), contract.inputs[operands.size()]);
        const auto lookup_indices = intrinsic == SIMDIntrinsic::Lookup;
        const auto execution = site.enter_operand_execution(state.completes);
        auto built = contract.stages[operands.size()] == ParameterStage::Static
            ? co_await site.read_static_argument(argument, type)
            : co_await site.read_argument(
                  argument,
                  lookup_indices ? std::nullopt : std::optional<ConstructionTypeRef>(type)
              );
        if (!built) {
            co_return std::unexpected(built.error());
        }
        if (lookup_indices) {
            const auto actual = unwrap_type_alias(site.draft(), site.type(*built));
            auto indices = std::optional<BuiltinType>();
            if (const auto* id = std::get_if<TypeID>(&actual)) {
                const auto canonical = site.draft().type_copy(*id);
                if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value); builtin
                    && (builtin->kind == BuiltinType::U8x16
                        || builtin->kind == BuiltinType::U8x32)) {
                    indices = builtin->kind;
                }
            }
            if (!indices) {
                co_return std::unexpected(site.fail(
                    site.syntax().expression(argument).span,
                    DiagnosticCode::TypeMismatch,
                    "SIMD lookup indices must be u8x16 or u8x32"
                ));
            }
            contract = simd_contract(intrinsic, owner, indices);
            type = resolve_simd_type(site.draft(), contract.inputs[operands.size()]);
        }
        auto converted =
            site.convert_argument(*built, type, site.syntax().expression(argument).span);
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
                if (!simd_static_control_valid(
                        intrinsic,
                        owner,
                        fact,
                        [&](ConstantID id) noexcept -> const ConstantFact& {
                            return site.draft().constant(id);
                        }
                    )) {
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
