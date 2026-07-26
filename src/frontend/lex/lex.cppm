module carven:frontend.lex;

import :diagnostics.diagnosed;
import :frontend.lex.token;
import :source.text;

auto lex(SourceView source) noexcept -> Diagnosed<TokenBuffer>;
