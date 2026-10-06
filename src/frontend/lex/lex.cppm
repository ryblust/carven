module carven:frontend.lex;

import :diagnostics.diagnosed;
import :frontend.lex.token;
import :source.text;
import :support.timing;

auto lex(SourceView source, TimingOutput timings = {}) noexcept -> Diagnosed<TokenBuffer>;
