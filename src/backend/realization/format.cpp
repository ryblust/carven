module carven:backend.realization.format.impl;

import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.format;
import :backend.realization.format;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
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

auto realize_writer_format(
    ModuleLowering& context,
    const SemanticExpression& source,
    const SemFormat& value,
    const PreparedWriterFormat& preparation,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr {
    const auto& format = preparation.format;
    const auto offset = value.receiver ? 1uz : 0uz;
    if (operands.size() != format.fields.size() + offset
        || format.text.size() != format.fields.size() + 1uz) {
        invariant_violation("writer format received the wrong operand pack");
    }
    const auto output = TargetIdentifier::from_spelling("output");
    const auto writer = TargetIdentifier::from_spelling("writer");
    const auto string_type = context.intrinsic_type(TargetSymbol::RuntimeString);
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
    auto arguments = std::vector<TargetExpr>();
    auto text_sizes = std::vector<TargetExpr>();
    for (auto index = 0uz; index < format.fields.size(); ++index) {
        const auto argument = TargetIdentifier::from_spelling(std::format("arg{}", index));
        const auto original = preparation.operand_indices[index];
        parameters.push_back(
            {.name = argument,
             .type = context.lower_parameter(
                 {.access = AccessMode::Read,
                  .type = value.operands[original].expression.type.resolved()}
             )}
        );
        arguments.push_back(name_expression(argument));
        const auto* type = std::get_if<BuiltinType>(&format.fields[index]);
        if (type != nullptr && (*type == BuiltinType::Str || *type == BuiltinType::String)) {
            text_sizes.push_back(call_member(name_expression(argument), "size", {}));
        }
    }
    auto writes = realize_writer_statements(
        context,
        format,
        writer,
        name_expression(output),
        std::move(arguments),
        std::move(text_sizes)
    );
    for (auto& statement : writes) {
        statements.push_back(std::move(statement));
    }
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

auto realize_writer_statements(
    ModuleLowering& context,
    const WriterFormat& format,
    TargetIdentifier writer,
    TargetExpr output,
    std::vector<TargetExpr> operands,
    std::vector<TargetExpr> text_sizes
) noexcept -> std::vector<TargetStmt> {
    if (operands.size() != format.fields.size()
        || format.text.size() != format.fields.size() + 1uz) {
        invariant_violation("writer format received the wrong field pack");
    }
    const auto text_fields =
        std::ranges::count_if(format.fields, [](const auto& field) static noexcept {
            const auto* type = std::get_if<BuiltinType>(&field);
            return type != nullptr && (*type == BuiltinType::Str || *type == BuiltinType::String);
        });
    if (text_sizes.size() != static_cast<std::size_t>(text_fields)) {
        invariant_violation("writer format received the wrong text length pack");
    }
    const auto writer_type = context.intrinsic_type(TargetSymbol::RuntimeWriter);
    auto statements = std::vector<TargetStmt>();
    auto writer_arguments = target_expressions(
        std::move(output),
        size_expression(format.minimum_size),
        size_expression(format.maximum_size)
    );
    if (!text_sizes.empty()) {
        const auto sizes_type = context.target().intern_type(
            {.value =
                 TargetIntrinsicType {
                     .symbol = TargetSymbol::StdInitializerList,
                     .type_argument_ids = {context.intrinsic_type(TargetSymbol::StdSize)}
                 },
             .const_qualified = false}
        );
        writer_arguments.push_back(
            TargetExpr {
                .value = TargetConstructionExpr {
                    .type = sizes_type,
                    .initializer = std::move(text_sizes)
                }
            }
        );
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
                    .initializer = std::move(writer_arguments),
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
        auto argument = std::move(operands[index]);
        const auto* builtin = std::get_if<BuiltinType>(&format.fields[index]);
        append_text(format.text[index]);
        if (const auto* field = std::get_if<IntegerFormatField>(&format.fields[index])) {
            statements.push_back(statement_expression(template_call_expression(
                member_expression(
                    name_expression(writer),
                    TargetIdentifier::from_spelling("integer")
                ),
                {integer_literal(static_cast<std::uint64_t>(field->base)),
                 field->uppercase,
                 field->zero_pad},
                target_expressions(std::move(argument), size_expression(field->width))
            )));
        } else {
            const auto method = *builtin == BuiltinType::Bool ? "boolean"
                : *builtin == BuiltinType::Char               ? "character"
                                                              : "append";
            statements.push_back(statement_expression(call_member(
                name_expression(writer),
                method,
                target_expressions(std::move(argument))
            )));
        }
    }
    append_text(format.text.back());
    return statements;
}

auto realize_format(
    ModuleLowering& context,
    const SemanticExpression& source,
    const SemFormat& value,
    const PreparedFormat& preparation,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr {
    if (const auto* writer = std::get_if<PreparedWriterFormat>(&preparation)) {
        return realize_writer_format(context, source, value, *writer, std::move(operands));
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
