module carven:frontend.parse.type.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.parse.builder;
import :frontend.parse.parser;
import :source.text;
import std;

auto Parser::parse_type() noexcept -> std::optional<ASTTypeID> {
    const auto nesting = enter_syntax_nesting();
    if (!nesting) {
        return std::nullopt;
    }
    if (check(TokenKind::Identifier) && slice(source, current().span) == "ptr") {
        const auto start = consume();
        expect(TokenKind::Less, "expected '<' after ptr");
        auto access = ASTAccessSyntax {.mode = ASTAccessMode::Read, .marker = std::nullopt};
        if (const auto marker = match(TokenKind::Ampersand)) {
            access = {.mode = ASTAccessMode::Write, .marker = marker->span};
        }
        if (check(TokenKind::AmpersandAmpersand)) {
            fail_here("ptr target access permits only Read or Write, not Take");
            return std::nullopt;
        }
        const auto target = parse_type();
        if (!target) {
            return std::nullopt;
        }
        auto end = current().span;
        if (check(TokenKind::RightShift)) {
            end = Span::from_bounds(end.start(), end.start() + 1);
            split_right_shift = true;
        } else {
            end = expect(TokenKind::Greater, "expected '>' after ptr target").span;
        }
        return builder.append_type(
            ASTType {
                .span = join(start.span, end),
                .value = ASTPointerType {.target = *target, .access = access},
            }
        );
    }
    if (check(TokenKind::Identifier) || check(TokenKind::ColonColon)) {
        return parse_named_type();
    }
    if (check(TokenKind::LeftBracket)) {
        return parse_sequence_type();
    }
    if (check(TokenKind::LeftParen)) {
        return parse_tuple_type();
    }
    if (check(TokenKind::Fn)) {
        return parse_function_type();
    }
    fail_here("expected type");
    return std::nullopt;
}

auto Parser::parse_tuple_type() noexcept -> std::optional<ASTTypeID> {
    const auto left = consume();
    auto elements = std::vector<ASTTypeID>();
    if (!check(TokenKind::RightParen)) {
        const auto first = parse_type();
        if (!first) {
            return std::nullopt;
        }
        if (!match(TokenKind::Comma)) {
            expect(TokenKind::RightParen, "expected ')' after grouped type");
            return failed ? std::nullopt : std::optional(*first);
        }
        elements.push_back(*first);
        while (!failed && !check(TokenKind::RightParen)) {
            const auto element = parse_type();
            if (!element) {
                return std::nullopt;
            }
            elements.push_back(*element);
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
    }
    const auto right = expect(TokenKind::RightParen, "expected ')' after tuple type");
    if (failed) {
        return std::nullopt;
    }
    return builder.append_type(
        ASTType {
            .span = join(left.span, right.span),
            .value = ASTTupleType {.elements = std::move(elements)}
        }
    );
}

auto Parser::parse_named_type() noexcept -> std::optional<ASTTypeID> {
    auto parsed = parse_named_type_form();
    return builder.append_type(
        ASTType {
            .span = parsed.span,
            .value = std::move(parsed.value),
        }
    );
}

auto Parser::parse_generic_argument() noexcept -> std::optional<ASTGenericArgument> {
    const auto nesting = enter_syntax_nesting();
    if (!nesting) {
        return std::nullopt;
    }
    auto span = current().span;
    auto value = std::optional<std::variant<ASTTypeID, ASTThrowClause>>();
    if (check(TokenKind::Throw)) {
        auto clause = parse_throw_clause();
        span = clause.span;
        value = std::move(clause);
    } else {
        const auto argument = parse_type();
        if (!argument) {
            return std::nullopt;
        }
        span = builder.type(*argument).span;
        value = *argument;
    }
    if (failed) {
        return std::nullopt;
    }

    return ASTGenericArgument {
        .span = span,
        .value = std::move(*value),
    };
}

auto Parser::parse_type_arguments() noexcept -> ParsedTypeForm<std::vector<ASTGenericArgument>> {
    const auto left = expect(TokenKind::Less, "expected '<' before type arguments");
    auto arguments = std::vector<ASTGenericArgument>();
    while (!failed) {
        auto argument = parse_generic_argument();
        if (!argument) {
            break;
        }
        arguments.push_back(std::move(*argument));
        if (!match(TokenKind::Comma) || check(TokenKind::Greater) || check(TokenKind::RightShift)) {
            break;
        }
    }
    auto end = current().span;
    if (check(TokenKind::RightShift)) {
        end = Span::from_bounds(end.start(), end.start() + 1);
        split_right_shift = true;
    } else {
        end = expect(TokenKind::Greater, "expected '>' after type arguments").span;
    }
    return {.span = join(left.span, end), .value = std::move(arguments)};
}

auto Parser::parse_named_type_form() noexcept -> ParsedTypeForm<ASTNamedType> {
    const auto global_root = match(TokenKind::ColonColon);
    const auto start = global_root ? expect_name_component("expected type name")
                                   : expect(TokenKind::Identifier, "expected type name");
    auto components = std::vector<ASTTypeNameComponent> {};
    components.push_back({.name_span = start.span});
    auto end = start.span;

    while (match(TokenKind::ColonColon)) {
        const auto name = expect_name_component("expected type name after '::'");
        components.push_back({.name_span = name.span});
        end = name.span;
    }

    auto arguments = std::vector<ASTGenericArgument>();
    if (check(TokenKind::Less)) {
        auto parsed = parse_type_arguments();
        arguments = std::move(parsed.value);
        end = parsed.span;
    }
    return ParsedTypeForm<ASTNamedType> {
        .span = join(global_root.has_value() ? global_root->span : start.span, end),
        .value = ASTNamedType {
            .global_root =
                global_root.has_value() ? std::optional(global_root->span) : std::nullopt,
            .components = std::move(components),
            .arguments = std::move(arguments)
        },
    };
}

auto Parser::parse_sequence_type() noexcept -> std::optional<ASTTypeID> {
    const auto left = expect(TokenKind::LeftBracket, "expected '['");
    const auto element = parse_type();
    if (!element) {
        return std::nullopt;
    }
    if (const auto right = match(TokenKind::RightBracket)) {
        return builder.append_type(
            ASTType {
                .span = join(left.span, right->span),
                .value = ASTSliceType {.element_type = *element},
            }
        );
    }
    expect(TokenKind::Semicolon, "expected ';' or ']' after element type");
    const auto extent = parse_expression();
    const auto right = expect(TokenKind::RightBracket, "expected ']' after array extent");
    if (!extent || failed) {
        return std::nullopt;
    }
    return builder.append_type(
        ASTType {
            .span = join(left.span, right.span),
            .value = ASTArrayType {.element_type = *element, .extent = *extent},
        }
    );
}

auto Parser::parse_array_type_form() noexcept -> std::optional<ParsedTypeForm<ASTArrayType>> {
    const auto left = expect(TokenKind::LeftBracket, "expected '['");
    const auto element_type = parse_type();
    if (!element_type) {
        return std::nullopt;
    }
    expect(TokenKind::Semicolon, "expected ';' after array element type");
    const auto extent = parse_expression();
    const auto right = expect(TokenKind::RightBracket, "expected ']' after array extent");
    if (!extent || failed) {
        return std::nullopt;
    }
    return ParsedTypeForm<ASTArrayType> {
        .span = join(left.span, right.span),
        .value = ASTArrayType {
            .element_type = *element_type,
            .extent = *extent,
        },
    };
}

auto Parser::parse_function_type() noexcept -> std::optional<ASTTypeID> {
    auto parsed = parse_function_type_form();
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    return builder.append_type(
        ASTType {
            .span = parsed->span,
            .value = std::move(parsed->value),
        }
    );
}

auto Parser::parse_function_type_form() noexcept -> std::optional<ParsedTypeForm<ASTFunctionType>> {
    const auto keyword = expect(TokenKind::Fn, "expected 'fn'");
    expect(TokenKind::LeftParen, "expected '(' after 'fn' in function type");
    auto parameters = std::vector<ASTFunctionTypeParameter> {};

    while (!failed && !check(TokenKind::RightParen)) {
        auto access = ASTAccessSyntax {.mode = ASTAccessMode::Read, .marker = std::nullopt};
        if (const auto marker = match(TokenKind::Ampersand)) {
            access = {.mode = ASTAccessMode::Write, .marker = marker->span};
        } else if (const auto marker = match(TokenKind::AmpersandAmpersand)) {
            access = {.mode = ASTAccessMode::Take, .marker = marker->span};
        }
        const auto type = parse_type();
        if (!type) {
            break;
        }
        auto start = builder.type(*type).span;
        if (access.marker.has_value()) {
            start = *access.marker;
        }
        parameters.push_back({
            .span = join(start, builder.type(*type).span),
            .access = access,
            .type = *type,
        });
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightParen)) {
            break;
        }
    }

    expect(TokenKind::RightParen, "expected ')' after function type parameters");
    expect(TokenKind::Arrow, "expected '->' in function type");
    const auto result_type = parse_type();
    if (!result_type) {
        return std::nullopt;
    }
    auto throw_clause = std::optional<ASTThrowClause> {};
    if (check(TokenKind::Throw)) {
        throw_clause = parse_throw_clause();
    }
    const auto end =
        throw_clause.has_value() ? throw_clause->span : builder.type(*result_type).span;

    return ParsedTypeForm<ASTFunctionType> {
        .span = join(keyword.span, end),
        .value = ASTFunctionType {
            .parameters = std::move(parameters),
            .result_type = *result_type,
            .throw_clause = std::move(throw_clause),
        },
    };
}

auto Parser::parse_throw_clause() noexcept -> ASTThrowClause {
    const auto keyword = expect(TokenKind::Throw, "expected 'throw'");
    auto failures = std::vector<ASTTypeID> {};
    auto plus_spans = std::vector<Span> {};
    if (match(TokenKind::LeftParen)) {
        const auto end = expect(TokenKind::RightParen, "expected ')' in empty failure clause");
        return {
            .span = join(keyword.span, end.span),
            .keyword_span = keyword.span,
            .failures = {},
            .plus_spans = {},
        };
    }
    const auto first = parse_named_type();
    if (first.has_value()) {
        failures.push_back(*first);
    }
    while (!failed) {
        const auto plus = match(TokenKind::Plus);
        if (!plus) {
            break;
        }
        plus_spans.push_back(plus->span);
        const auto failure = parse_named_type();
        if (failure.has_value()) {
            failures.push_back(*failure);
        }
    }
    const auto end = failed || failures.empty() ? keyword.span : builder.type(failures.back()).span;
    return {
        .span = join(keyword.span, end),
        .keyword_span = keyword.span,
        .failures = std::move(failures),
        .plus_spans = std::move(plus_spans),
    };
}
