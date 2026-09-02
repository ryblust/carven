module carven:frontend.parse.grammar.type.impl;

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
import :frontend.parse.context;
import :source.text;
import std;

auto Parser::parse_type() noexcept -> std::optional<ASTTypeID> {
    const auto nesting = enter_syntax_nesting();
    if (!nesting) {
        return std::nullopt;
    }
    if (check(TokenKind::Identifier)) {
        return parse_named_type();
    }
    if (check(TokenKind::LeftBracket)) {
        return parse_array_type();
    }
    if (check(TokenKind::Fn)) {
        return parse_function_type();
    }
    fail_here("expected type");
    return std::nullopt;
}

auto Parser::parse_named_type() noexcept -> std::optional<ASTTypeID> {
    const auto start = expect(TokenKind::Identifier, "expected type name");
    auto components = std::vector<ASTTypeNameComponent> {};
    components.push_back({.name_span = start.span});
    auto end = start.span;

    while (match(TokenKind::ColonColon)) {
        const auto name = expect(TokenKind::Identifier, "expected type name after '::'");
        components.push_back({.name_span = name.span});
        end = name.span;
    }

    return builder.append_type(
        ASTType {
            .span = join(start.span, end),
            .value = ASTNamedType {.components = std::move(components)},
        }
    );
}

auto Parser::parse_array_type() noexcept -> std::optional<ASTTypeID> {
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
    return builder.append_type(
        ASTType {
            .span = join(left.span, right.span),
            .value = ASTArrayType {
                .element_type = *element_type,
                .extent = *extent,
            },
        }
    );
}

auto Parser::parse_function_type() noexcept -> std::optional<ASTTypeID> {
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

    return builder.append_type(
        ASTType {
            .span = join(keyword.span, end),
            .value = ASTFunctionType {
                .parameters = std::move(parameters),
                .result_type = *result_type,
                .throw_clause = std::move(throw_clause),
            },
        }
    );
}

auto Parser::parse_throw_clause() noexcept -> ASTThrowClause {
    const auto keyword = expect(TokenKind::Throw, "expected 'throw'");
    auto failures = std::vector<ASTTypeID> {};
    auto plus_spans = std::vector<Span> {};
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
