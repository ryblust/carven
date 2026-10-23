module carven:frontend.lex;

import :diagnostics.diagnosed;
import :frontend.lex.token;
import :source.text;
import :support.timing;

// Errors retain Invalid regions and following tokens; diagnostics still gate compilation.
auto lex(SourceView source, TimingOutput timings = {}) noexcept -> Diagnosed<TokenBuffer>;
