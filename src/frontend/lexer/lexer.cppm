export module zero.frontend.lexer;

import zero.frontend.lexer.token;
import std;

export auto tokenize(std::string_view source) noexcept -> std::vector<Token>;
