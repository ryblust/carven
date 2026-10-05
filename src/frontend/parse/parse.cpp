module carven:frontend.parse.impl;

import :frontend.parse;
import :frontend.parse.parser;
import :support.timing;

auto parse(
    const SourceManager& sources,
    const TokenBuffer& tokens,
    const TimingOutput& timings
) noexcept -> std::expected<SyntaxTree, Diagnostics> {
    const auto scope = TimingScope(timings, TimingStage::Parsing);
    return Parser(sources.view(tokens.source_id()), tokens).run();
}
