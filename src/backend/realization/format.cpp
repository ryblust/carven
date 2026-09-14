module carven:backend.realization.format.impl;

import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.format;
import :backend.realization.format;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import std;

namespace {

auto integer_literal(std::uint64_t value) noexcept -> TargetIntegerLiteral {
    return {.negative = false, .magnitude = value, .suffix = TargetIntegerSuffix::None};
}

auto size_expression(std::uint64_t value) noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = integer_literal(value)}};
}

auto realize_integer_format(
    ModuleLowering& context,
    const SemanticExpression& source,
    const SemFormat& value,
    const PreparedIntegerFormat& preparation,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr {
    const auto& format = preparation.format;
    const auto offset = value.receiver ? 1uz : 0uz;
    if (operands.size() != format.fields.size() + offset
        || format.text.size() != format.fields.size() + 1uz) {
        invariant_violation("integer format received the wrong operand pack");
    }
    const auto output = TargetIdentifier::from_spelling("output");
    const auto writer = TargetIdentifier::from_spelling("writer");
    const auto string_type = context.intrinsic_type(TargetSymbol::RuntimeString);
    const auto writer_type = context.intrinsic_type(TargetSymbol::RuntimeWriter);
    auto parameters = std::vector<TargetLambdaParameter>();
    auto statements = std::vector<TargetStmt>();
    if (value.receiver) {
        parameters.push_back({.name = output, .type = context.reference_type(string_type)});
    } else {
        statements.push_back(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = output,
                .type = string_type,
                .initializer = TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = string_type,
                        .initializer = {},
                    }
                },
            }
        ));
    }
    statements.push_back(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = writer,
            .type = writer_type,
            .initializer = TargetExpr {
                .value = TargetConstructionExpr {
                    .type = writer_type,
                    .initializer = target_expressions(
                        name_expression(output),
                        size_expression(format.minimum_size),
                        size_expression(format.maximum_size)
                    ),
                }
            },
        }
    ));
    const auto append_text = [&](std::string_view text) noexcept {
        if (!text.empty()) {
            statements.push_back(statement_expression(call_member(
                name_expression(writer),
                "append",
                target_expressions(
                    TargetExpr {
                        .value = TargetLiteralExpr {
                            .value = TargetStringLiteral {
                                .bytes = std::string(text),
                                .kind = TargetStringLiteralKind::StringView,
                            },
                        }
                    }
                )
            )));
        }
    };
    for (auto index = 0uz; index < format.fields.size(); ++index) {
        const auto argument = TargetIdentifier::from_spelling(std::format("arg{}", index));
        const auto original = preparation.operand_indices[index];
        parameters.push_back({
            .name = argument,
            .type = context.lower_type(value.operands[original].expression.type.resolved()),
        });
        append_text(format.text[index]);
        const auto& field = format.fields[index];
        statements.push_back(statement_expression(template_call_expression(
            member_expression(name_expression(writer), TargetIdentifier::from_spelling("integer")),
            {integer_literal(static_cast<std::uint64_t>(field.base)),
             field.uppercase,
             field.zero_pad},
            target_expressions(name_expression(argument), size_expression(field.width))
        )));
    }
    append_text(format.text.back());
    if (!value.receiver) {
        statements.push_back(generated_statement(
            TargetReturnStmt {
                .expression = name_expression(output),
            }
        ));
    }
    // All prepared values enter before any write. The lambda preserves that
    // completion boundary even when C++ leaves argument evaluation unordered.
    return call_expression(
        TargetExpr {
            .value =
                TargetLambdaExpr {
                    .parameters = std::move(parameters),
                    .result = context.lower_type(source.type.resolved()),
                    .body = std::move(statements),
                }
        },
        std::move(operands)
    );
}

} // namespace

auto realize_format(
    ModuleLowering& context,
    const SemanticExpression& source,
    const SemFormat& value,
    const PreparedFormat& preparation,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr {
    if (const auto* integer = std::get_if<PreparedIntegerFormat>(&preparation)) {
        return realize_integer_format(context, source, value, *integer, std::move(operands));
    }

    if (const auto* text = std::get_if<PreparedFormatText>(&preparation)) {
        const auto literal = [&]() noexcept -> TargetExpr {
            return {
                .value = TargetLiteralExpr {
                    .value = TargetStringLiteral {
                        .bytes = text->text,
                        .kind = TargetStringLiteralKind::StringView
                    }
                }
            };
        };
        if (operands.size() != (value.receiver ? 1uz : 0uz)) {
            invariant_violation("precomputed formatting retained operand values");
        }
        if (value.receiver) {
            return call_member(
                std::move(operands.front()),
                "append",
                target_expressions(literal())
            );
        }
        operands.push_back(literal());
        return call_expression(
            static_member_expression(
                context.lower_type(source.type.resolved()),
                TargetIdentifier::from_spelling("from_str")
            ),
            std::move(operands)
        );
    }
    const auto& delegated = std::get<PreparedDelegatedFormat>(preparation);
    operands.insert(
        operands.begin() + (value.receiver ? 1uz : 0uz),
        TargetExpr {
            .value = TargetLiteralExpr {
                .value = TargetStringLiteral {
                    .bytes = delegated.format_string,
                    .kind = TargetStringLiteralKind::StringView,
                }
            }
        }
    );
    const auto proved = delegated.encoding == FormatResultEncoding::ValidUTF8;
    const auto symbol = value.receiver
        ? (proved ? TargetSymbol::RuntimeAppendFormatValidUTF8 : TargetSymbol::RuntimeAppendFormat)
        : (proved ? TargetSymbol::RuntimeFormatValidUTF8 : TargetSymbol::RuntimeFormat);
    return call_expression(intrinsic_expression(symbol), std::move(operands));
}
