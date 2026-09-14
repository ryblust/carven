module carven:frontend.parse.impl;

import :frontend.parse.parser;
import :frontend.parse;

auto parse(const SourceManager& sources, const TokenBuffer& tokens) noexcept
    -> std::expected<SyntaxTree, Diagnostics> {
    return Parser(sources.view(tokens.source_id()), tokens).run();
}
