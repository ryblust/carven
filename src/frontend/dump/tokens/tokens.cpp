module carven:frontend.dump.tokens.impl;

import :frontend.dump.text;
import :frontend.dump.tokens;
import :frontend.lex.token;
import :source.manager;
import std;

auto render_token_dump(const SourceManager& sources, const TokenBuffer& buffer) noexcept
    -> std::string {
    const auto source_view = sources.view(buffer.source_id());
    const auto tokens = buffer.tokens();
    auto output = std::format("Tokens {}\n", quote_dump_text(source_view.origin));
    if (tokens.empty()) {
        append_dump_line(output, {}, true, "<empty>");
        return output;
    }

    for (auto index = 0uz; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        append_dump_line(
            output,
            {},
            index + 1 == tokens.size(),
            std::format("{} {}", token.kind, format_source_label(source_view.text, token.span))
        );
    }
    return output;
}
