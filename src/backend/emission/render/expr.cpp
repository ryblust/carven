module carven:backend.emission.render.expr.impl;

import :backend.emission.render.string;
import :backend.emission.render;
import :backend.target.symbol;
import :support.visit;
import std;

namespace {

auto precedence(TargetBinaryOperator op) noexcept -> TargetPrecedence {
    switch (op) {
        case TargetBinaryOperator::LogicalOr:    return TargetPrecedence::LogicalOr;
        case TargetBinaryOperator::LogicalAnd:   return TargetPrecedence::LogicalAnd;
        case TargetBinaryOperator::BitwiseOr:    return TargetPrecedence::BitwiseOr;
        case TargetBinaryOperator::BitwiseXor:   return TargetPrecedence::BitwiseXor;
        case TargetBinaryOperator::BitwiseAnd:   return TargetPrecedence::BitwiseAnd;
        case TargetBinaryOperator::Equal:
        case TargetBinaryOperator::NotEqual:     return TargetPrecedence::Equality;
        case TargetBinaryOperator::Less:
        case TargetBinaryOperator::LessEqual:
        case TargetBinaryOperator::Greater:
        case TargetBinaryOperator::GreaterEqual: return TargetPrecedence::Relational;
        case TargetBinaryOperator::LeftShift:
        case TargetBinaryOperator::RightShift:   return TargetPrecedence::Shift;
        case TargetBinaryOperator::Add:
        case TargetBinaryOperator::Subtract:     return TargetPrecedence::Additive;
        case TargetBinaryOperator::Multiply:
        case TargetBinaryOperator::Divide:
        case TargetBinaryOperator::Remainder:    return TargetPrecedence::Multiplicative;
    }
    std::unreachable();
}

auto prefix_spelling(TargetPrefixOperator op) noexcept -> std::string_view {
    switch (op) {
        case TargetPrefixOperator::Increment:   return "++";
        case TargetPrefixOperator::Decrement:   return "--";
        case TargetPrefixOperator::AddressOf:   return "&";
        case TargetPrefixOperator::Dereference: return "*";
        case TargetPrefixOperator::LogicalNot:  return "!";
        case TargetPrefixOperator::Negate:      return "-";
        case TargetPrefixOperator::BitwiseNot:  return "~";
    }
    std::unreachable();
}

auto binary_spelling(TargetBinaryOperator op) noexcept -> std::string_view {
    switch (op) {
        case TargetBinaryOperator::LogicalOr:    return "||";
        case TargetBinaryOperator::LogicalAnd:   return "&&";
        case TargetBinaryOperator::BitwiseOr:    return "|";
        case TargetBinaryOperator::BitwiseXor:   return "^";
        case TargetBinaryOperator::BitwiseAnd:   return "&";
        case TargetBinaryOperator::Equal:        return "==";
        case TargetBinaryOperator::NotEqual:     return "!=";
        case TargetBinaryOperator::Less:         return "<";
        case TargetBinaryOperator::LessEqual:    return "<=";
        case TargetBinaryOperator::Greater:      return ">";
        case TargetBinaryOperator::GreaterEqual: return ">=";
        case TargetBinaryOperator::LeftShift:    return "<<";
        case TargetBinaryOperator::RightShift:   return ">>";
        case TargetBinaryOperator::Add:          return "+";
        case TargetBinaryOperator::Subtract:     return "-";
        case TargetBinaryOperator::Multiply:     return "*";
        case TargetBinaryOperator::Divide:       return "/";
        case TargetBinaryOperator::Remainder:    return "%";
    }
    std::unreachable();
}

auto literal_spelling(const TargetLiteralValue& literal) noexcept -> std::string {
    return std::visit(
        Overloaded {
            [](bool value) static noexcept { return std::string {value ? "true" : "false"}; },
            [](const TargetIntegerLiteral& value) static noexcept {
                if (value.negative
                    && value.suffix == TargetIntegerSuffix::LongLong
                    && value.magnitude == (1ull << 63)) {
                    return std::string {"(-9223372036854775807ll - 1ll)"};
                }
                auto result = std::to_string(value.magnitude);
                switch (value.suffix) {
                    case TargetIntegerSuffix::None:             break;
                    case TargetIntegerSuffix::Unsigned:         result += 'u'; break;
                    case TargetIntegerSuffix::LongLong:         result += "ll"; break;
                    case TargetIntegerSuffix::UnsignedLongLong: result += "ull"; break;
                }
                if (value.negative) {
                    result.insert(result.begin(), '-');
                }
                return result;
            },
            [](const TargetFloatLiteral& value) static noexcept {
                return std::visit(
                    Overloaded {
                        [](float number) static noexcept {
                            auto result = std::format("{:.9g}", number);
                            if (result.find_first_of(".eE") == std::string::npos) {
                                result += ".0";
                            }
                            result += 'f';
                            return result;
                        },
                        [](double number) static noexcept {
                            auto result = std::format("{:.17g}", number);
                            if (result.find_first_of(".eE") == std::string::npos) {
                                result += ".0";
                            }
                            return result;
                        },
                    },
                    value.value
                );
            },
            [](const TargetCharacterLiteral& value) static noexcept {
                return std::format("char32_t{{0x{:X}}}", static_cast<std::uint32_t>(value.scalar));
            },
            [](const TargetStringLiteral& value) static noexcept {
                auto result = cpp_string_token(value.bytes);
                switch (value.kind) {
                    case TargetStringLiteralKind::String: break;
                    case TargetStringLiteralKind::StringView:
                        result = value.bytes.empty()
                            ? std::format("std::string_view{{{}}}", result)
                            : std::format("std::string_view{{{}, {}}}", result, value.bytes.size());
                        break;
                }
                return result;
            },
        },
        literal
    );
}

auto expression_precedence(const TargetExpr& expression) noexcept -> TargetPrecedence {
    return std::visit(
        Overloaded {
            [](const TargetBinaryExpr& value) static noexcept { return precedence(value.op); },
            [](const TargetConditionalExpr&) static noexcept {
                return TargetPrecedence::Conditional;
            },
            [](const TargetPrefixExpr&) static noexcept { return TargetPrecedence::Prefix; },
            [](const TargetCallExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetIndexExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetMemberExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetScopeMemberExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetStaticMemberExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetStaticCastExpr&) static noexcept { return TargetPrecedence::Postfix; },
            []<typename Value>(const Value&) static noexcept {
                static_assert(
                    std::same_as<Value, TargetNameExpr>
                        || std::same_as<Value, TargetIntrinsicNameExpr>
                        || std::same_as<Value, TargetLiteralExpr>
                        || std::same_as<Value, TargetArrayExpr>
                        || std::same_as<Value, TargetConstructionExpr>
                        || std::same_as<Value, TargetLambdaExpr>,
                    "unhandled target expression precedence"
                );
                return TargetPrecedence::Primary;
            },
        },
        expression.value
    );
}

} // namespace

auto TargetRenderer::render_expression(
    const TargetExpr& expression,
    TargetPrecedence parent
) noexcept -> LayoutNodeID {
    const auto own_precedence = expression_precedence(expression);
    auto rendered = std::visit(
        Overloaded {
            [&](const TargetNameExpr& name) noexcept { return this->render_name(name.name); },
            [&](const TargetIntrinsicNameExpr& intrinsic) noexcept {
                return text(target_symbol_spelling(intrinsic.symbol));
            },
            [&](const TargetLiteralExpr& literal) noexcept {
                return text(literal_spelling(literal.value));
            },
            [&](const TargetPrefixExpr& prefix) noexcept {
                auto spelling = std::string(prefix_spelling(prefix.op));
                const auto* nested = std::get_if<TargetPrefixExpr>(&prefix.operand->value);
                if (nested != nullptr && prefix_spelling(nested->op).front() == spelling.back()) {
                    spelling += ' ';
                }
                return concat(
                    {text(spelling), render_expression(*prefix.operand, TargetPrecedence::Prefix)}
                );
            },
            [&](const TargetBinaryExpr& binary) noexcept {
                const auto right_precedence =
                    static_cast<TargetPrecedence>(static_cast<std::uint8_t>(own_precedence) + 1);
                const auto left = render_expression(*binary.left, own_precedence);
                const auto right = concat(
                    {text(binary_spelling(binary.op)),
                     text(" "),
                     render_expression(*binary.right, right_precedence)}
                );
                return choice(
                    {concat({left, text(" "), right}),
                     concat({left, builder.indent(indent_width, concat({builder.line(), right}))})}
                );
            },
            [&](const TargetConditionalExpr& conditional) noexcept {
                return concat(
                    {render_expression(*conditional.condition, TargetPrecedence::LogicalOr),
                     text(" ? "),
                     render_expression(*conditional.true_value),
                     text(" : "),
                     render_expression(*conditional.false_value, TargetPrecedence::Conditional)}
                );
            },
            [&](const TargetCallExpr& call) noexcept {
                auto templates = std::vector<LayoutNodeID> {};
                for (const auto argument : call.template_argument_type_ids) {
                    templates.push_back(render_type(argument));
                }
                auto arguments = std::vector<LayoutNodeID> {};
                for (const auto& argument : call.arguments) {
                    arguments.push_back(render_expression(argument));
                }
                auto callee = render_expression(*call.callee, TargetPrecedence::Postfix);
                if (!templates.empty()) {
                    callee = concat({callee, delimited_list(templates, "<", ">")});
                }
                return concat({callee, delimited_list(arguments, "(", ")")});
            },
            [&](const TargetArrayExpr& array) noexcept {
                const auto template_values = std::array {
                    render_type(array.element_type_id),
                    render_expression(*array.extent)
                };
                auto values = std::vector<LayoutNodeID> {};
                for (const auto& element : array.elements) {
                    values.push_back(render_expression(element));
                }
                return concat(
                    {text("std::array"),
                     delimited_list(template_values, "<", ">"),
                     delimited_list(values, "{", "}")}
                );
            },
            [&](const TargetConstructionExpr& construction) noexcept {
                auto values = std::vector<LayoutNodeID> {};
                std::visit(
                    Overloaded {
                        [](const std::monostate&) static noexcept {},
                        [&](const std::vector<TargetExpr>& positional) noexcept {
                            for (const auto& element : positional) {
                                values.push_back(render_expression(element));
                            }
                        },
                        [&](const std::vector<TargetFieldInitializer>& fields) noexcept {
                            for (const auto& field : fields) {
                                values.push_back(concat(
                                    {text("."),
                                     render_identifier(field.name),
                                     text(" = "),
                                     render_expression(*field.value)}
                                ));
                            }
                        },
                    },
                    construction.initializer
                );
                return concat({render_type(construction.type), delimited_list(values, "{", "}")});
            },
            [&](const TargetIndexExpr& index) noexcept {
                const auto argument = std::array {render_expression(*index.index)};
                return concat(
                    {render_expression(*index.operand, TargetPrecedence::Postfix),
                     delimited_list(argument, "[", "]")}
                );
            },
            [&](const TargetMemberExpr& member) noexcept {
                const auto name = std::visit(
                    Overloaded {
                        [&](const TargetIdentifier& value) noexcept {
                            return render_identifier(value);
                        },
                        [&](const TargetRawIdentifier& value) noexcept {
                            return text(value.spelling);
                        },
                    },
                    member.name
                );
                const auto operand = render_expression(*member.operand, TargetPrecedence::Postfix);
                const auto suffix = concat({text("."), name});
                return choice(
                    {concat({operand, suffix}),
                     concat(
                         {operand, builder.indent(indent_width, concat({builder.line(), suffix}))}
                     )}
                );
            },
            [&](const TargetScopeMemberExpr& member) noexcept {
                const auto operand = render_expression(*member.operand, TargetPrecedence::Postfix);
                const auto name = std::visit(
                    Overloaded {
                        [&](const TargetIdentifier& value) noexcept {
                            return render_identifier(value);
                        },
                        [&](const TargetRawIdentifier& value) noexcept {
                            return text(value.spelling);
                        },
                    },
                    member.name
                );
                const auto suffix = concat({text("::"), name});
                return choice(
                    {concat({operand, suffix}),
                     concat(
                         {operand, builder.indent(indent_width, concat({builder.line(), suffix}))}
                     )}
                );
            },
            [&](const TargetStaticMemberExpr& member) noexcept {
                const auto owner = render_type(member.owner);
                const auto suffix = concat({text("::"), render_identifier(member.name)});
                return choice(
                    {concat({owner, suffix}),
                     concat(
                         {owner, builder.indent(indent_width, concat({builder.line(), suffix}))}
                     )}
                );
            },
            [&](const TargetStaticCastExpr& cast) noexcept {
                const auto operand = std::array {render_expression(*cast.operand)};
                return concat(
                    {text("static_cast<"),
                     render_type(cast.type),
                     text(">"),
                     delimited_list(operand, "(", ")")}
                );
            },
            [&](const TargetLambdaExpr& region) noexcept {
                auto parameters = std::vector<LayoutNodeID>();
                for (const auto& parameter : region.parameters) {
                    parameters.push_back(concat(
                        {render_type(parameter.type), text(" "), render_identifier(parameter.name)}
                    ));
                }
                return concat({
                    text("([&]"),
                    delimited_list(parameters, "(", ")"),
                    text(" noexcept -> "),
                    render_type(region.result),
                    text(" "),
                    render_statement_block(region.body),
                    text(")"),
                });
            },

        },
        expression.value
    );
    if (own_precedence < parent) {
        rendered = concat({text("("), rendered, text(")")});
    }
    return rendered;
}
