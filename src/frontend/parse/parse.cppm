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
    const TimingOutput& timings = {}
) noexcept -> std::expected<SyntaxTree, Diagnostics>;

// Retains complete top-level items around recoverable syntax errors. A null
// tree means delimiter preflight or the initial import section failed. This
// result is for source queries; diagnostics must still gate compilation.
auto parse_recovering(
    const SourceManager& sources,
    const TokenBuffer& tokens,
    const TimingOutput& timings = {}
) noexcept -> Diagnosed<std::optional<SyntaxTree>>;
