module carven:frontend.parse;

import :diagnostics.diagnostic;
import :frontend.ast.tree;
import :frontend.lex.token;
import :source.manager;
import :source.text;
import :support.timing;
import std;

auto parse(
    const SourceManager& sources,
    const TokenBuffer& tokens,
    TimingOutput timings = {}
) noexcept -> std::expected<SyntaxTree, Diagnostics>;
