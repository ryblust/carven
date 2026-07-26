module carven:frontend.dump.tokens;

import :frontend.lex.token;
import :source.manager;
import std;

auto render_token_dump(const SourceManager& sources, const TokenBuffer& tokens) noexcept
    -> std::string;
