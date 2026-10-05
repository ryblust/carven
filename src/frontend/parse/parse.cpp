module carven:frontend.parse.impl;

import :diagnostics.diagnosed;
import :frontend.parse;
import :frontend.parse.parser;
import :support.timing;
import std;

auto parse(
    const SourceManager& sources,
    const TokenBuffer& tokens,
    const TimingOutput& timings
) noexcept -> std::expected<SyntaxTree, Diagnostics> {
    auto recovered = parse_recovering(sources, tokens, timings);
    if (!recovered.value || !recovered.diagnostics.empty()) {
        return std::unexpected(std::move(recovered.diagnostics));
    }
    return std::move(*recovered.value);
}

auto parse_recovering(
    const SourceManager& sources,
    const TokenBuffer& tokens,
    const TimingOutput& timings
) noexcept -> Diagnosed<std::optional<SyntaxTree>> {
    const auto scope = TimingScope(timings, TimingStage::Parsing);
    return Parser(sources.view(tokens.source_id()), tokens).run();
}
