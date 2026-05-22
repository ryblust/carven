#include "doctest.h"

import carven.frontend.token;
import carven.frontend.lexer;

TEST_CASE("Testing Lexer") {
    const auto tokens = tokenize("import std;");
    CHECK_EQ(tokens[0].kind, TokenKind::Keyword);
}
