#include "doctest.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import std;

TEST_CASE("Lexer: keywords") {
    SUBCASE("import") {
        const auto tokens = tokenize("import");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Import);
        CHECK_EQ(text_at("import", tokens[0].span), "import");
    }

    SUBCASE("all 17 keywords recognized as correct TokenKind") {
        static constexpr auto keywords = std::array<std::pair<std::string_view, TokenKind>, 17> {{
            { "import", TokenKind::Import },
            { "export", TokenKind::Export },
            { "using",  TokenKind::Using  },
            { "fn",     TokenKind::Fn     },
            { "struct", TokenKind::Struct },
            { "enum",   TokenKind::Enum   },
            { "var",    TokenKind::Var    },
            { "let",    TokenKind::Let    },
            { "const",  TokenKind::Const  },
            { "if",     TokenKind::If     },
            { "else",   TokenKind::Else   },
            { "for",    TokenKind::For    },
            { "while",  TokenKind::While  },
            { "as",     TokenKind::As     },
            { "true",   TokenKind::True   },
            { "false",  TokenKind::False  },
            { "return", TokenKind::Return },
        }};

        for (const auto [text, kind] : keywords) {
            const auto tokens = tokenize(text);
            CHECK_EQ(tokens.size(), 1u);
            CHECK_EQ(tokens[0].kind, kind);
            CHECK_EQ(text_at(text, tokens[0].span), text);
        }
    }
}

TEST_CASE("Lexer: identifiers") {
    SUBCASE("simple identifier") {
        const auto tokens = tokenize("hello");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(text_at("hello", tokens[0].span), "hello");
    }

    SUBCASE("identifier with underscores and digits") {
        const auto tokens = tokenize("my_var_123");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(text_at("my_var_123", tokens[0].span), "my_var_123");
    }

    SUBCASE("identifier starting with underscore") {
        const auto tokens = tokenize("_hidden");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
    }

    SUBCASE("identifier containing keyword prefix") {
        const auto tokens = tokenize("importing");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
    }
}

TEST_CASE("Lexer: number literals") {
    SUBCASE("integer") {
        const auto tokens = tokenize("42");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(text_at("42", tokens[0].span), "42");
    }

    SUBCASE("zero") {
        const auto tokens = tokenize("0");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(text_at("0", tokens[0].span), "0");
    }

    SUBCASE("float") {
        const auto tokens = tokenize("3.14");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(text_at("3.14", tokens[0].span), "3.14");
    }

    SUBCASE("float starting with zero") {
        const auto tokens = tokenize("0.5");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(text_at("0.5", tokens[0].span), "0.5");
    }

    SUBCASE("large integer") {
        const auto tokens = tokenize("1234567890");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(text_at("1234567890", tokens[0].span), "1234567890");
    }

    SUBCASE("dot followed by non-digit is integer then dot") {
        const auto tokens = tokenize("42.x");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(text_at("42.x", tokens[0].span), "42");
        CHECK_EQ(tokens[1].kind, TokenKind::Dot);
    }
}

TEST_CASE("Lexer: char literals") {
    SUBCASE("simple char") {
        const auto tokens = tokenize("'a'");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::CharLiteral);
        CHECK_EQ(text_at("'a'", tokens[0].span), "'a'");
    }

    SUBCASE("escaped single quote") {
        const auto tokens = tokenize("'\\''");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::CharLiteral);
        CHECK_EQ(text_at("'\\''", tokens[0].span), "'\\''");
    }

    SUBCASE("escaped backslash") {
        const auto tokens = tokenize("'\\\\'");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::CharLiteral);
    }

    SUBCASE("newline char") {
        const auto tokens = tokenize("'\\n'");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::CharLiteral);
    }

    SUBCASE("unterminated char at newline") {
        const auto tokens = tokenize("'a\nb");
        CHECK_EQ(tokens.size(), 2u);
        CHECK_EQ(tokens[0].kind, TokenKind::CharLiteral);
        // unterminated char consumes 'a but stops at newline
    }
}

TEST_CASE("Lexer: string literals") {
    SUBCASE("simple string") {
        const auto tokens = tokenize("\"hello\"");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
        CHECK_EQ(text_at("\"hello\"", tokens[0].span), "\"hello\"");
    }

    SUBCASE("empty string") {
        const auto tokens = tokenize("\"\"");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
        CHECK_EQ(text_at("\"\"", tokens[0].span), "\"\"");
    }

    SUBCASE("string with escaped quote") {
        const auto tokens = tokenize("\"say \\\"hi\\\"\"");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
    }

    SUBCASE("string with escaped backslash") {
        const auto tokens = tokenize("\"path\\\\to\\\\file\"");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
    }

    SUBCASE("unterminated string stops at newline") {
        const auto tokens = tokenize("\"hello\nworld\"");
        CHECK(tokens.size() >= 1);
        CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
        // "hello is consumed, \n stops it
    }
}

TEST_CASE("Lexer: punctuation") {
    SUBCASE("comma")          { const auto t = tokenize(",");  CHECK_EQ(t[0].kind, TokenKind::Comma);       }
    SUBCASE("dot")            { const auto t = tokenize(".");  CHECK_EQ(t[0].kind, TokenKind::Dot);         }
    SUBCASE("colon")          { const auto t = tokenize(":");  CHECK_EQ(t[0].kind, TokenKind::Colon);       }
    SUBCASE("semicolon")      { const auto t = tokenize(";");  CHECK_EQ(t[0].kind, TokenKind::SemiColon);   }
    SUBCASE("left paren")     { const auto t = tokenize("(");  CHECK_EQ(t[0].kind, TokenKind::LeftParen);   }
    SUBCASE("right paren")    { const auto t = tokenize(")");  CHECK_EQ(t[0].kind, TokenKind::RightParen);  }
    SUBCASE("left bracket")   { const auto t = tokenize("[");  CHECK_EQ(t[0].kind, TokenKind::LeftBracket); }
    SUBCASE("right bracket")  { const auto t = tokenize("]");  CHECK_EQ(t[0].kind, TokenKind::RightBracket);}
    SUBCASE("left brace")     { const auto t = tokenize("{");  CHECK_EQ(t[0].kind, TokenKind::LeftBrace);   }
    SUBCASE("right brace")    { const auto t = tokenize("}");  CHECK_EQ(t[0].kind, TokenKind::RightBrace);  }
}

TEST_CASE("Lexer: single-char operators") {
    SUBCASE("plus")     { const auto t = tokenize("+"); CHECK_EQ(t[0].kind, TokenKind::Plus);    }
    SUBCASE("minus")    { const auto t = tokenize("-"); CHECK_EQ(t[0].kind, TokenKind::Minus);   }
    SUBCASE("star")     { const auto t = tokenize("*"); CHECK_EQ(t[0].kind, TokenKind::Star);    }
    SUBCASE("slash")    { const auto t = tokenize("/"); CHECK_EQ(t[0].kind, TokenKind::Slash);   }
    SUBCASE("percent")  { const auto t = tokenize("%"); CHECK_EQ(t[0].kind, TokenKind::Percent); }
    SUBCASE("bang")     { const auto t = tokenize("!"); CHECK_EQ(t[0].kind, TokenKind::Bang);    }
    SUBCASE("equal")    { const auto t = tokenize("="); CHECK_EQ(t[0].kind, TokenKind::Equal);   }
    SUBCASE("less")     { const auto t = tokenize("<"); CHECK_EQ(t[0].kind, TokenKind::Less);    }
    SUBCASE("greater")  { const auto t = tokenize(">"); CHECK_EQ(t[0].kind, TokenKind::Greater); }
    SUBCASE("tilde")    { const auto t = tokenize("~"); CHECK_EQ(t[0].kind, TokenKind::Tilde);   }
    SUBCASE("ampersand"){ const auto t = tokenize("&"); CHECK_EQ(t[0].kind, TokenKind::Ampersand);}
    SUBCASE("pipe")     { const auto t = tokenize("|"); CHECK_EQ(t[0].kind, TokenKind::Pipe);     }
    SUBCASE("caret")    { const auto t = tokenize("^"); CHECK_EQ(t[0].kind, TokenKind::Caret);    }
}

TEST_CASE("Lexer: multi-char operators") {
    SUBCASE("plus equal")          { const auto t = tokenize("+="); CHECK_EQ(t[0].kind, TokenKind::PlusEqual);       }
    SUBCASE("minus equal")         { const auto t = tokenize("-="); CHECK_EQ(t[0].kind, TokenKind::MinusEqual);      }
    SUBCASE("star equal")          { const auto t = tokenize("*="); CHECK_EQ(t[0].kind, TokenKind::StarEqual);       }
    SUBCASE("slash equal")         { const auto t = tokenize("/="); CHECK_EQ(t[0].kind, TokenKind::SlashEqual);      }
    SUBCASE("percent equal")       { const auto t = tokenize("%="); CHECK_EQ(t[0].kind, TokenKind::PercentEqual);    }
    SUBCASE("bang equal")          { const auto t = tokenize("!="); CHECK_EQ(t[0].kind, TokenKind::BangEqual);       }
    SUBCASE("equal equal")         { const auto t = tokenize("=="); CHECK_EQ(t[0].kind, TokenKind::EqualEqual);      }
    SUBCASE("less equal")          { const auto t = tokenize("<="); CHECK_EQ(t[0].kind, TokenKind::LessEqual);       }
    SUBCASE("greater equal")       { const auto t = tokenize(">="); CHECK_EQ(t[0].kind, TokenKind::GreaterEqual);    }
    SUBCASE("plus plus")           { const auto t = tokenize("++"); CHECK_EQ(t[0].kind, TokenKind::PlusPlus);        }
    SUBCASE("minus minus")         { const auto t = tokenize("--"); CHECK_EQ(t[0].kind, TokenKind::MinusMinus);      }
    SUBCASE("ampersand ampersand") { const auto t = tokenize("&&"); CHECK_EQ(t[0].kind, TokenKind::AmpersandAmpersand);}
    SUBCASE("pipe pipe")           { const auto t = tokenize("||"); CHECK_EQ(t[0].kind, TokenKind::PipePipe);        }
    SUBCASE("left shift")          { const auto t = tokenize("<<"); CHECK_EQ(t[0].kind, TokenKind::LeftShift);       }
    SUBCASE("right shift")         { const auto t = tokenize(">>"); CHECK_EQ(t[0].kind, TokenKind::RightShift);      }
    SUBCASE("ampersand equal")     { const auto t = tokenize("&="); CHECK_EQ(t[0].kind, TokenKind::AmpersandEqual);   }
    SUBCASE("pipe equal")          { const auto t = tokenize("|="); CHECK_EQ(t[0].kind, TokenKind::PipeEqual);        }
    SUBCASE("caret equal")         { const auto t = tokenize("^="); CHECK_EQ(t[0].kind, TokenKind::CaretEqual);       }
    SUBCASE("left shift equal")    { const auto t = tokenize("<<=");CHECK_EQ(t[0].kind, TokenKind::LeftShiftEqual);   }
    SUBCASE("right shift equal")   { const auto t = tokenize(">>=");CHECK_EQ(t[0].kind, TokenKind::RightShiftEqual);  }
    SUBCASE("arrow")               { const auto t = tokenize("->"); CHECK_EQ(t[0].kind, TokenKind::Arrow);            }
    SUBCASE("fat arrow")           { const auto t = tokenize("=>"); CHECK_EQ(t[0].kind, TokenKind::FatArrow);         }
    SUBCASE("colon colon")         { const auto t = tokenize("::"); CHECK_EQ(t[0].kind, TokenKind::ColonColon);       }
}

TEST_CASE("Lexer: comments") {
    SUBCASE("line comment consumed entirely") {
        const auto tokens = tokenize("// this is a comment");
        CHECK(tokens.empty());
    }

    SUBCASE("comment followed by code") {
        const auto tokens = tokenize("// comment\na");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(text_at("// comment\na", tokens[0].span), "a");
    }

    SUBCASE("code followed by comment") {
        const auto tokens = tokenize("x = 1 // trailing comment");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(tokens[1].kind, TokenKind::Equal);
        CHECK_EQ(tokens[2].kind, TokenKind::NumberLiteral);
    }

    SUBCASE("division operator not confused with comment") {
        const auto tokens = tokenize("a/b");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[1].kind, TokenKind::Slash);
        CHECK_EQ(tokens[2].kind, TokenKind::Identifier);
    }
}

TEST_CASE("Lexer: whitespace handling") {
    SUBCASE("spaces between tokens") {
        const auto tokens = tokenize("a   +   b");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(tokens[1].kind, TokenKind::Plus);
        CHECK_EQ(tokens[2].kind, TokenKind::Identifier);
    }

    SUBCASE("tabs between tokens") {
        const auto tokens = tokenize("a\t+\tb");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(tokens[1].kind, TokenKind::Plus);
        CHECK_EQ(tokens[2].kind, TokenKind::Identifier);
    }

    SUBCASE("newlines between tokens") {
        const auto tokens = tokenize("a\n+\nb");
        CHECK_EQ(tokens.size(), 3u);
    }

    SUBCASE("carriage return") {
        const auto tokens = tokenize("a\r+\rb");
        CHECK_EQ(tokens.size(), 3u);
    }

    SUBCASE("mixed whitespace") {
        const auto tokens = tokenize("a \t\n\r + b");
        CHECK_EQ(tokens.size(), 3u);
    }
}

TEST_CASE("Lexer: edge cases") {
    SUBCASE("empty input produces no tokens") {
        const auto tokens = tokenize("");
        CHECK(tokens.empty());
    }

    SUBCASE("only whitespace produces no tokens") {
        const auto tokens = tokenize("  \t\n\r  ");
        CHECK(tokens.empty());
    }

    SUBCASE("only comment produces no tokens") {
        const auto tokens = tokenize("// nothing here");
        CHECK(tokens.empty());
    }

    SUBCASE("unknown character") {
        const auto tokens = tokenize("@");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Unknown);
    }

    SUBCASE("multiple unknown characters") {
        const auto tokens = tokenize("@#$");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[0].kind, TokenKind::Unknown);
        CHECK_EQ(tokens[1].kind, TokenKind::Unknown);
        CHECK_EQ(tokens[2].kind, TokenKind::Unknown);
    }
}

TEST_CASE("Lexer: token spans") {
    SUBCASE("adjacent tokens have correct non-overlapping spans") {
        static constexpr auto source = std::string_view("let x = 1;");
        const auto tokens = tokenize(source);
        CHECK_EQ(tokens.size(), 5u);
        // let
        CHECK_EQ(tokens[0].span.start, 0u);
        CHECK_EQ(tokens[0].span.end, 3u);
        CHECK_EQ(text_at(source, tokens[0].span), "let");
        // x
        CHECK_EQ(tokens[1].span.start, 4u);
        CHECK_EQ(tokens[1].span.end, 5u);
        CHECK_EQ(text_at(source, tokens[1].span), "x");
        // =
        CHECK_EQ(tokens[2].span.start, 6u);
        CHECK_EQ(tokens[2].span.end, 7u);
        CHECK_EQ(text_at(source, tokens[2].span), "=");
        // 1
        CHECK_EQ(tokens[3].span.start, 8u);
        CHECK_EQ(tokens[3].span.end, 9u);
        CHECK_EQ(text_at(source, tokens[3].span), "1");
        // ;
        CHECK_EQ(tokens[4].span.start, 9u);
        CHECK_EQ(tokens[4].span.end, 10u);
        CHECK_EQ(text_at(source, tokens[4].span), ";");
    }

    SUBCASE("multi-char token span covers full operator") {
        static constexpr auto source = std::string_view("a += b");
        const auto tokens = tokenize(source);
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[1].kind, TokenKind::PlusEqual);
        CHECK_EQ(text_at(source, tokens[1].span), "+=");
        CHECK_EQ(tokens[1].span.end - tokens[1].span.start, 2u);
    }
}

TEST_CASE("Lexer: tokenize import std") {
    static constexpr auto source = std::string_view("import std;");
    const auto tokens = tokenize(source);
    CHECK_EQ(tokens.size(), 3u);
    CHECK_EQ(tokens[0].kind, TokenKind::Import);
    CHECK_EQ(text_at(source, tokens[0].span), "import");
    CHECK_EQ(tokens[1].kind, TokenKind::Identifier);
    CHECK_EQ(text_at(source, tokens[1].span), "std");
    CHECK_EQ(tokens[2].kind, TokenKind::SemiColon);
}
