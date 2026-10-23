module carven:frontend.parse;

import :diagnostics.diagnosed;
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

// Retains complete imports/items/statements around errors and missing closing
// delimiters. Unrecoverable delimiter pairing or nesting can leave no tree.
// Diagnostics still gate compilation; retained syntax supports source queries.
auto parse_recovering(
    const SourceManager& sources,
    const TokenBuffer& tokens,
    TimingOutput timings = {}
) noexcept -> Diagnosed<std::optional<SyntaxTree>>;
