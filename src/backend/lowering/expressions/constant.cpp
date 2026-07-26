module carven:backend.lowering.expressions.constant.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target.expr;
import :semantic.hir;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import std;

auto lower_constant_value(TargetCallableLowerer& context, HIRConstantID constant) noexcept
    -> TargetExprID {
    const auto& fact = context.semantic().constant(constant);
    return std::visit(
        [&](const auto& value) noexcept -> TargetExprID {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, HIRIntegerConstant>) {
                return context.target().append_expression({
                    .value = TargetLiteralExpr {
                        .value = lower_literal(
                            context,
                            HIRIntegerLiteralValue {
                                .negative = value.negative(),
                                .magnitude = value.magnitude(),
                            },
                            fact.type
                        ),
                    },
                });
            } else if constexpr (std::same_as<Value, HIRBooleanConstant>) {
                return context.target().append_expression({
                    .value = TargetLiteralExpr {.value = value.value},
                });
            } else if constexpr (std::same_as<Value, HIRStringConstant>) {
                return context.target().append_expression({
                    .value = TargetLiteralExpr {
                        .value = TargetStringLiteral {
                            .bytes =
                                std::string(context.semantic().provenance().spelling(value.value)),
                            .kind = TargetStringLiteralKind::StringView,
                        },
                    },
                });
            } else if constexpr (std::same_as<Value, HIRFloatingConstant>) {
                auto literal = HIRLiteralValue {HIRF64LiteralValue {.value = value.value}};
                if (const auto* builtin =
                        std::get_if<HIRBuiltinTypeValue>(&context.semantic().type(fact.type).value);
                    builtin != nullptr && builtin->kind == HIRBuiltinType::F32) {
                    literal = HIRF32LiteralValue {.value = static_cast<float>(value.value)};
                }
                return context.target().append_expression({
                    .value = TargetLiteralExpr {
                        .value = lower_literal(context, literal, fact.type),
                    },
                });
            } else if constexpr (std::same_as<Value, HIRCharacterConstant>) {
                return context.target().append_expression({
                    .value = TargetLiteralExpr {
                        .value = TargetCharacterLiteral {.scalar = value.scalar},
                    },
                });
            } else if constexpr (std::same_as<Value, HIRNumericEnumConstant>) {
                return name_expression(
                    context,
                    symbol_reference_name(
                        context,
                        context.semantic().enum_case(value.enum_case).symbol
                    )
                );
            } else if constexpr (std::same_as<Value, HIRPayloadEnumConstant>) {
                const auto callee = name_expression(
                    context,
                    symbol_reference_name(
                        context,
                        context.semantic().enum_case(value.enum_case).symbol
                    )
                );
                if (value.payload.empty()) {
                    return callee;
                }
                auto arguments = std::vector<TargetExprID>();
                arguments.reserve(value.payload.size());
                for (const auto payload : value.payload) {
                    arguments.push_back(lower_constant_value(context, payload));
                }
                return call_expression(context, callee, std::move(arguments));
            } else {
                invariant_violation("unsupported normalized constant fact");
            }
        },
        fact.value
    );
}
