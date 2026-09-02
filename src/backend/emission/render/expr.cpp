module carven:backend.emission.render.expr.impl;

import :backend.emission.render;
import :backend.emission.render.string;
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
        default:                                 return TargetPrecedence::Multiplicative;
    }
}

auto prefix_spelling(TargetPrefixOperator op) noexcept -> std::string_view {
    switch (op) {
        case TargetPrefixOperator::AddressOf:  return "&";
        case TargetPrefixOperator::LogicalNot: return "!";
        case TargetPrefixOperator::Negate:     return "-";
        case TargetPrefixOperator::BitwiseNot: return "~";
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
                if (value.kind == TargetStringLiteralKind::StringView) {
                    result = std::format("std::string_view{{{}, {}}}", result, value.bytes.size());
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
            [](const TargetPrefixExpr&) static noexcept { return TargetPrecedence::Prefix; },
            [](const TargetCallExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetIndexExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetMemberExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetScopeMemberExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetStaticMemberExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetForwardExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const TargetStaticCastExpr&) static noexcept { return TargetPrecedence::Postfix; },
            [](const auto&) static noexcept { return TargetPrecedence::Primary; },
        },
        expression.value
    );
}

} // namespace

auto TargetRenderer::render_expression(TargetExprID id, TargetPrecedence parent) noexcept
    -> LayoutNodeID {
    const auto& value = unit.expression(id);
    const auto own_precedence = expression_precedence(value);
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
                const auto* nested =
                    std::get_if<TargetPrefixExpr>(&unit.expression(prefix.operand_id).value);
                if (nested != nullptr && prefix_spelling(nested->op).front() == spelling.back()) {
                    spelling += ' ';
                }
                return concat(
                    {text(spelling), render_expression(prefix.operand_id, TargetPrecedence::Prefix)}
                );
            },
            [&](const TargetBinaryExpr& binary) noexcept {
                const auto right_precedence =
                    static_cast<TargetPrecedence>(static_cast<std::uint8_t>(own_precedence) + 1);
                const auto left = render_expression(binary.left, own_precedence);
                const auto right = concat(
                    {text(binary_spelling(binary.op)),
                     text(" "),
                     render_expression(binary.right, right_precedence)}
                );
                return choice(
                    {concat({left, text(" "), right}),
                     concat({left, builder.indent(indent_width, concat({builder.line(), right}))})}
                );
            },
            [&](const TargetCallExpr& call) noexcept {
                auto templates = std::vector<LayoutNodeID> {};
                for (const auto argument : call.template_argument_type_ids) {
                    templates.push_back(render_type(argument));
                }
                auto arguments = std::vector<LayoutNodeID> {};
                for (const auto argument : call.arguments) {
                    arguments.push_back(render_expression(argument));
                }
                auto callee = render_expression(call.callee, TargetPrecedence::Postfix);
                if (!templates.empty()) {
                    callee = concat({callee, delimited_list(templates, "<", ">")});
                }
                return concat({callee, delimited_list(arguments, "(", ")")});
            },
            [&](const TargetArrayExpr& array) noexcept {
                const auto template_values = std::array {
                    render_type(array.element_type_id),
                    render_expression(array.extent)
                };
                auto values = std::vector<LayoutNodeID> {};
                for (const auto element : array.element_ids) {
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
                        [&](const std::vector<TargetExprID>& positional) noexcept {
                            for (const auto element : positional) {
                                values.push_back(render_expression(element));
                            }
                        },
                        [&](const std::vector<TargetFieldInitializer>& fields) noexcept {
                            for (const auto& field : fields) {
                                values.push_back(concat(
                                    {text("."),
                                     render_identifier(field.name),
                                     text(" = "),
                                     render_expression(field.value)}
                                ));
                            }
                        },
                    },
                    construction.initializer
                );
                return concat({render_type(construction.type), delimited_list(values, "{", "}")});
            },
            [&](const TargetIndexExpr& index) noexcept {
                const auto argument = std::array {render_expression(index.index)};
                return concat(
                    {render_expression(index.operand_id, TargetPrecedence::Postfix),
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
                const auto operand =
                    render_expression(member.operand_id, TargetPrecedence::Postfix);
                const auto suffix = concat({text("."), name});
                return choice(
                    {concat({operand, suffix}),
                     concat(
                         {operand, builder.indent(indent_width, concat({builder.line(), suffix}))}
                     )}
                );
            },
            [&](const TargetScopeMemberExpr& member) noexcept {
                const auto operand =
                    render_expression(member.operand_id, TargetPrecedence::Postfix);
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
            [&](const TargetForwardExpr& forward) noexcept {
                const auto operand = render_identifier(forward.name);
                return concat(
                    {text(target_symbol_spelling(TargetSymbol::StdForward)),
                     text("<decltype("),
                     operand,
                     text(")>("),
                     operand,
                     text(")")}
                );
            },
            [&](const TargetStaticCastExpr& cast) noexcept {
                const auto operand = std::array {render_expression(cast.operand_id)};
                return concat(
                    {text("static_cast<"),
                     render_type(cast.type),
                     text(">"),
                     delimited_list(operand, "(", ")")}
                );
            },
            [&](const TargetLambdaExpr& lambda) noexcept {
                return concat(
                    {text("("),
                     text("[&]() noexcept "),
                     render_statement_block(lambda.body),
                     text("()"),
                     text(")")}
                );
            },
            [&](const TargetClosureExpr& closure) noexcept {
                auto captures = std::vector<LayoutNodeID> {};
                for (const auto& capture : closure.captures) {
                    auto value = concat(
                        {text(capture.mode == TargetCaptureMode::Write ? "&" : ""),
                         render_identifier(capture.name)}
                    );
                    if (capture.name != capture.source) {
                        value = concat({value, text(" = "), render_identifier(capture.source)});
                    }
                    captures.push_back(value);
                }
                auto parameters = std::vector<LayoutNodeID> {};
                for (const auto& parameter : closure.parameters) {
                    if (!parameter.name.has_value()) {
                        parameters.push_back(render_type(parameter.type));
                    } else {
                        parameters.push_back(concat({
                            render_type(parameter.type),
                            text(" "),
                            render_identifier(*parameter.name),
                        }));
                    }
                }
                const auto result = render_type_layouts(closure.result);
                return concat({
                    delimited_list(captures, "[", "]"),
                    delimited_list(parameters, "(", ")"),
                    render_trailing_return(result, false),
                    text(" "),
                    render_statement_block(closure.body),
                });
            },
        },
        value.value
    );
    if (own_precedence < parent) {
        rendered = concat({text("("), rendered, text(")")});
    }
    return rendered;
}
