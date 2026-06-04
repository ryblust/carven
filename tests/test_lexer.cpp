#include "test_helpers.h"

import carven.frontend.token;
import carven.frontend.lexer;
import std;

#define CHECK_LEXEME_EQ(source, span, expected) \
    CHECK_EQ(std::string_view(source).substr((span).start, (span).end - (span).start), (expected))

TEST_CASE("Lexer: keywords") {
    SUBCASE("all 18 keywords recognized as correct TokenKind") {
        static constexpr auto keywords = std::array<std::pair<std::string_view, TokenKind>, 18> {{
            { "import", TokenKind::Import },
            { "export", TokenKind::Export },
            { "using",  TokenKind::Using  },
            { "fn",     TokenKind::Fn     },
            { "struct", TokenKind::Struct },
            { "enum",   TokenKind::Enum   },
            { "var",    TokenKind::Var    },
            { "let",    TokenKind::Let    },
            { "match",  TokenKind::Match  },
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
            CHECK_LEXEME_EQ(text, tokens[0].span, text);
        }
    }
}

TEST_CASE("Lexer: identifiers") {
    SUBCASE("identifiers") {
        CHECK_EQ(tokenize("hello")[0].kind, TokenKind::Identifier);
        const auto t = tokenize("my_var_123");
        CHECK_EQ(t[0].kind, TokenKind::Identifier);
        CHECK_LEXEME_EQ("my_var_123", t[0].span, "my_var_123");
        CHECK_EQ(tokenize("_hidden")[0].kind, TokenKind::Identifier);
        CHECK_EQ(tokenize("importing")[0].kind, TokenKind::Identifier);
    }
}

TEST_CASE("Lexer: number literals") {
    SUBCASE("integer") {
        const auto tokens = tokenize("42");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::NumberLiteral);
        CHECK_LEXEME_EQ("42", tokens[0].span, "42");
    }
    SUBCASE("float and dot-disambiguation") {
        CHECK_EQ(tokenize("3.14")[0].kind, TokenKind::NumberLiteral);
        const auto t = tokenize("42.x");
        CHECK_EQ(t.size(), 3u);
        CHECK_EQ(t[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(t[1].kind, TokenKind::Dot);
    }
}

TEST_CASE("Lexer: char literals") {
    SUBCASE("char literals") {
        CHECK_EQ(tokenize("'a'")[0].kind, TokenKind::CharLiteral);
        CHECK_EQ(tokenize("'\\''")[0].kind, TokenKind::CharLiteral);
        CHECK_EQ(tokenize("'\\\\'")[0].kind, TokenKind::CharLiteral);
        CHECK_EQ(tokenize("'\\n'")[0].kind, TokenKind::CharLiteral);
    }
    SUBCASE("unterminated char at newline") {
        const auto tokens = tokenize("'a\nb");
        CHECK_EQ(tokens.size(), 2u);
        CHECK_EQ(tokens[0].kind, TokenKind::CharLiteral);
    }
}

TEST_CASE("Lexer: string literals") {
    SUBCASE("string literals") {
        CHECK_EQ(tokenize("\"hello\"")[0].kind, TokenKind::StringLiteral);
        CHECK_EQ(tokenize("\"\"")[0].kind, TokenKind::StringLiteral);
        CHECK_EQ(tokenize("\"say \\\"hi\\\"\"")[0].kind, TokenKind::StringLiteral);
        CHECK_EQ(tokenize("\"path\\\\to\\\\file\"")[0].kind, TokenKind::StringLiteral);
    }
    SUBCASE("unterminated string stops at newline") {
        const auto tokens = tokenize("\"hello\nworld\"");
        CHECK(tokens.size() >= 1);
        CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
    }
}

TEST_CASE("Lexer: punctuation") {
    SUBCASE("all punctuation") {
        CHECK_EQ(tokenize(",")[0].kind, TokenKind::Comma);
        CHECK_EQ(tokenize(".")[0].kind, TokenKind::Dot);
        CHECK_EQ(tokenize(":")[0].kind, TokenKind::Colon);
        CHECK_EQ(tokenize(";")[0].kind, TokenKind::SemiColon);
        CHECK_EQ(tokenize("(")[0].kind, TokenKind::LeftParen);
        CHECK_EQ(tokenize(")")[0].kind, TokenKind::RightParen);
        CHECK_EQ(tokenize("[")[0].kind, TokenKind::LeftBracket);
        CHECK_EQ(tokenize("]")[0].kind, TokenKind::RightBracket);
        CHECK_EQ(tokenize("{")[0].kind, TokenKind::LeftBrace);
        CHECK_EQ(tokenize("}")[0].kind, TokenKind::RightBrace);
    }
}

TEST_CASE("Lexer: single-char operators") {
    SUBCASE("all single-char operators") {
        CHECK_EQ(tokenize("+")[0].kind, TokenKind::Plus);
        CHECK_EQ(tokenize("-")[0].kind, TokenKind::Minus);
        CHECK_EQ(tokenize("*")[0].kind, TokenKind::Star);
        CHECK_EQ(tokenize("/")[0].kind, TokenKind::Slash);
        CHECK_EQ(tokenize("%")[0].kind, TokenKind::Percent);
        CHECK_EQ(tokenize("!")[0].kind, TokenKind::Bang);
        CHECK_EQ(tokenize("=")[0].kind, TokenKind::Equal);
        CHECK_EQ(tokenize("<")[0].kind, TokenKind::Less);
        CHECK_EQ(tokenize(">")[0].kind, TokenKind::Greater);
        CHECK_EQ(tokenize("~")[0].kind, TokenKind::Tilde);
        CHECK_EQ(tokenize("&")[0].kind, TokenKind::Ampersand);
        CHECK_EQ(tokenize("|")[0].kind, TokenKind::Pipe);
        CHECK_EQ(tokenize("^")[0].kind, TokenKind::Caret);
    }
}

TEST_CASE("Lexer: multi-char operators") {
    SUBCASE("all multi-char operators") {
        CHECK_EQ(tokenize("+=")[0].kind, TokenKind::PlusEqual);
        CHECK_EQ(tokenize("-=")[0].kind, TokenKind::MinusEqual);
        CHECK_EQ(tokenize("*=")[0].kind, TokenKind::StarEqual);
        CHECK_EQ(tokenize("/=")[0].kind, TokenKind::SlashEqual);
        CHECK_EQ(tokenize("%=")[0].kind, TokenKind::PercentEqual);
        CHECK_EQ(tokenize("!=")[0].kind, TokenKind::BangEqual);
        CHECK_EQ(tokenize("==")[0].kind, TokenKind::EqualEqual);
        CHECK_EQ(tokenize("<=")[0].kind, TokenKind::LessEqual);
        CHECK_EQ(tokenize(">=")[0].kind, TokenKind::GreaterEqual);
        CHECK_EQ(tokenize("++")[0].kind, TokenKind::PlusPlus);
        CHECK_EQ(tokenize("--")[0].kind, TokenKind::MinusMinus);
        CHECK_EQ(tokenize("&&")[0].kind, TokenKind::AmpersandAmpersand);
        CHECK_EQ(tokenize("||")[0].kind, TokenKind::PipePipe);
        CHECK_EQ(tokenize("<<")[0].kind, TokenKind::LeftShift);
        CHECK_EQ(tokenize(">>")[0].kind, TokenKind::RightShift);
        CHECK_EQ(tokenize("&=")[0].kind, TokenKind::AmpersandEqual);
        CHECK_EQ(tokenize("|=")[0].kind, TokenKind::PipeEqual);
        CHECK_EQ(tokenize("^=")[0].kind, TokenKind::CaretEqual);
        CHECK_EQ(tokenize("<<=")[0].kind, TokenKind::LeftShiftEqual);
        CHECK_EQ(tokenize(">>=")[0].kind, TokenKind::RightShiftEqual);
        CHECK_EQ(tokenize("->")[0].kind, TokenKind::Arrow);
        CHECK_EQ(tokenize("=>")[0].kind, TokenKind::FatArrow);
        CHECK_EQ(tokenize("::")[0].kind, TokenKind::ColonColon);
    }
}

TEST_CASE("Lexer: comments") {
    SUBCASE("line comment consumed entirely") {
        const auto tokens = tokenize("// this is a comment");
        CHECK(tokens.empty());
    }

    SUBCASE("comment then code") {
        const auto tokens = tokenize("// comment\nx");
        CHECK_EQ(tokens.size(), 1u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
    }

    SUBCASE("division operator not confused with comment") {
        const auto tokens = tokenize("a/b");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[1].kind, TokenKind::Slash);
    }
}

TEST_CASE("Lexer: whitespace handling") {
    SUBCASE("mixed whitespace between tokens") {
        const auto tokens = tokenize("a\t \n\r+\tb");
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(tokens[1].kind, TokenKind::Plus);
        CHECK_EQ(tokens[2].kind, TokenKind::Identifier);
    }
}

TEST_CASE("Lexer: edge cases") {
    SUBCASE("empty, whitespace, and comments produce no tokens") {
        CHECK(tokenize("").empty());
        CHECK(tokenize("  \t\n\r  ").empty());
        CHECK(tokenize("// nothing here").empty());
    }

    SUBCASE("unknown characters") {
        CHECK_EQ(tokenize("@")[0].kind, TokenKind::Unknown);
        const auto t = tokenize("@#$");
        CHECK_EQ(t.size(), 3u);
        CHECK_EQ(t[0].kind, TokenKind::Unknown);
        CHECK_EQ(t[2].kind, TokenKind::Unknown);
    }
}

TEST_CASE("Lexer: token spans") {
    SUBCASE("adjacent tokens have correct non-overlapping spans") {
        static constexpr auto source = std::string_view{"let x = 1;"};
        const auto tokens = tokenize(source);
        CHECK_EQ(tokens.size(), 5u);
        CHECK_EQ(tokens[0].span.start, 0u);
        CHECK_EQ(tokens[0].span.end, 3u);
        CHECK_LEXEME_EQ(source, tokens[0].span, "let");
        CHECK_EQ(tokens[1].span.start, 4u);
        CHECK_EQ(tokens[1].span.end, 5u);
        CHECK_LEXEME_EQ(source, tokens[1].span, "x");
        CHECK_EQ(tokens[2].span.start, 6u);
        CHECK_EQ(tokens[2].span.end, 7u);
        CHECK_LEXEME_EQ(source, tokens[2].span, "=");
        CHECK_EQ(tokens[3].span.start, 8u);
        CHECK_EQ(tokens[3].span.end, 9u);
        CHECK_LEXEME_EQ(source, tokens[3].span, "1");
        CHECK_EQ(tokens[4].span.start, 9u);
        CHECK_EQ(tokens[4].span.end, 10u);
        CHECK_LEXEME_EQ(source, tokens[4].span, ";");
    }

    SUBCASE("multi-char token span covers full operator") {
        static constexpr auto source = std::string_view{"a += b"};
        const auto tokens = tokenize(source);
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[1].span.start, 2u);
        CHECK_EQ(tokens[1].span.end, 4u);
        CHECK_LEXEME_EQ(source, tokens[1].span, "+=");
    }

    SUBCASE("import std; tokenized correctly") {
        static constexpr auto source = std::string_view{"import std;"};
        const auto tokens = tokenize(source);
        CHECK_EQ(tokens.size(), 3u);
        CHECK_EQ(tokens[0].kind, TokenKind::Import);
        CHECK_EQ(tokens[1].kind, TokenKind::Identifier);
        CHECK_EQ(tokens[2].kind, TokenKind::SemiColon);
    }
}

TEST_CASE("Lexer: number literals edge cases") {
    SUBCASE("number edge cases") {
        const auto t1 = tokenize(".5");
        CHECK_EQ(t1.size(), 2u);
        CHECK_EQ(t1[0].kind, TokenKind::Dot);
        CHECK_EQ(t1[1].kind, TokenKind::NumberLiteral);
        const auto t2 = tokenize("0.");
        CHECK_EQ(t2.size(), 2u);
        CHECK_EQ(t2[0].kind, TokenKind::NumberLiteral);
        CHECK_EQ(t2[1].kind, TokenKind::Dot);
    }
}

TEST_CASE("Lexer: char escape sequences") {
    SUBCASE("char escape sequences") {
        CHECK_EQ(tokenize("'\\t'")[0].kind, TokenKind::CharLiteral);
        CHECK_EQ(tokenize("'\\r'")[0].kind, TokenKind::CharLiteral);
    }
}

TEST_CASE("Lexer: string escape sequences") {
    SUBCASE("tab") {
        const auto tokens = tokenize("\"hello\\tworld\"");
        CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
        CHECK_LEXEME_EQ("\"hello\\tworld\"", tokens[0].span, "\"hello\\tworld\"");
    }
}

TEST_CASE("Lexer: operator disambiguation") {
    SUBCASE("&&& lexes as && then &") {
        const auto tokens = tokenize("&&&");
        CHECK_EQ(tokens.size(), 2u);
        CHECK_EQ(tokens[0].kind, TokenKind::AmpersandAmpersand);
        CHECK_EQ(tokens[1].kind, TokenKind::Ampersand);
    }
}
