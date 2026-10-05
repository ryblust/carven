module carven:frontend.lex;

import :diagnostics.diagnosed;
import :frontend.lex.token;
import :source.text;
import :support.timing;

auto lex(SourceView source, const TimingOutput& timings = {}) noexcept -> Diagnosed<TokenBuffer>;
