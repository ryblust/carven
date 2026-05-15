export module zero.frontend.parser;

import zero.frontend.token;
import zero.frontend.parser.ast;
import std;

export auto parse(std::span<const Token> tokens, std::string_view source) noexcept -> ParseResult;

export auto parse_expr_tokens(std::span<const Token> tokens, std::string_view source) noexcept -> ParseResult;
