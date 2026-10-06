module carven:test.internal.frontend.lex.literals;

import :frontend.lex;
import :frontend.lex.literal;
import :frontend.lex.token;
import :frontend.literal;
import :source.identifier;
import :source.text;
import :test.harness.framework;
import :test.internal.frontend.lex.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Lexer: numeric spellings follow the grammar exactly"_test = [] static noexcept {
        static constexpr auto valid = std::to_array<std::string_view>({
            "0",     "42",         "42i8",    "42i16",         "42i32",   "42i64",     "42u8",
            "42u16", "42u32",      "42u64",   "42isize",       "42usize", "1f32",      "1f64",
            "3.14",  "3.14e-2f32", "1E+9f64", "0xDeadBEEFu64", "0x1f32",  "0B1010i16", "0o755usize",
        });
        each(valid, std::identity {}, [](const auto& spelling) static noexcept {
            check_token(spelling, TokenKind::NumberLiteral);
        });

        static constexpr auto not_single_tokens = std::to_array<std::string_view>({
            ".5",
            "0.",
        });
        each(not_single_tokens, std::identity {}, [](const auto& spelling) static noexcept {
            check_not_single_number(spelling);
        });

        static constexpr auto malformed = std::to_array<std::string_view>({
            "1e",
            "0x",
            "0b102",
            "0o8",
            "1f",
            "1ul",
            "1.0i32",
        });
        each(malformed, std::identity {}, [](const auto& spelling) static noexcept {
            check_lexical_error(spelling);
        });
    };

    "Lexer: strings and characters accept only shared simple escapes"_test = [] static noexcept {
        static constexpr auto valid_characters = std::to_array<std::string_view>({
            "'a'",
            "'\\''",
            "'\\\"'",
            "'\\\\'",
            "'\\n'",
            "'\\t'",
            "'\\r'",
            "'\\0'",
            "'é'",
            "'你'",
            "'😀'",
            "'\\u{1F600}'",
        });
        static constexpr auto valid_strings = std::to_array<std::string_view>({
            "\"\"",
            "\"hello\"",
            "\"quote: \\\"\"",
            "\"slash: \\\\\"",
            "\"\\'\\n\\t\\r\\0\"",
            "\"你好\"",
        });
        each(valid_characters, std::identity {}, [](const auto& spelling) static noexcept {
            check_token(spelling, TokenKind::CharLiteral);
        });
        each(valid_strings, std::identity {}, [](const auto& spelling) static noexcept {
            check_token(spelling, TokenKind::StringLiteral);
        });

        static constexpr auto invalid = std::to_array<std::string_view>({
            "''",
            "'ab'",
            "'\\q'",
            "'é'",
            "'\\u{}'",
            "'\\u{D800}'",
            "'\\u{110000}'",
            "'\\x61'",
            "\"\\q\"",
            "\"\\01\"",
            "'\\07'",
            "'a\n'",
            "\"line\nbreak\"",
            "\"unterminated",
        });
        each(invalid, std::identity {}, [](const auto& spelling) static noexcept {
            check_lexical_error(spelling);
        });
    };

    "Lexer: C++ source fragments are line-fenced opaque tokens"_test = [] static noexcept {
        static constexpr auto valid = std::to_array<std::string_view>({
            "#[cpp] ---\n---",
            "#[cpp] ---\n{ if (ready) { call(); } }\n---",
            R"CV(#[cpp] ---
auto text = "}";
auto raw = R"tag({ // not Carven })tag";
---)CV",
            "#[cpp] ---\n// }\n/* { */\n#if 0\n}\n#endif\n---",
            "#[cpp] -----\n---\n-----",
            "#[cpp]\t---\r\nauto value = 1;\r\n\t---\t",
        });
        each(valid, std::identity {}, [](const auto& spelling) static noexcept {
            check_token(spelling, TokenKind::CppSourceFragment);
        });

        static constexpr auto invalid = std::to_array<std::string_view>({
            "#[ cpp] ---\n---",
            "#[cpp ] ---\n---",
            "#[cpp]",
            "#[cpp] --\n--",
            "#[cpp] --- trailing\n---",
            "#[cpp] ---\nnative();",
            "#[cpp] ----\nnative();\n---",
        });
        each(invalid, std::identity {}, [](const auto& spelling) static noexcept {
            check_lexical_error(spelling);
        });

        static constexpr auto early_close = std::string_view(
            "#[cpp] ---\n"
            "before();\n"
            "---\n"
            "after();\n"
        );
        const auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = early_close,
            .origin = "early-close.cv",
        };
        const auto lexed = lex(source);
        if (!expect(lexed.diagnostics.empty())) {
            return;
        }
        if (!expect(!lexed.value.tokens().empty())) {
            return;
        }
        expect_equal(lexed.value.tokens().front().kind, TokenKind::CppSourceFragment);
        expect_equal(
            slice(early_close, lexed.value.tokens().front().span),
            std::string_view("#[cpp] ---\nbefore();\n---")
        );
    };

    "Lexer: C++ header names retain their dedicated spelling"_test = [] static noexcept {
        static constexpr auto text =
            std::string_view("import <vendor/api.hpp>; import \"native/provider.hpp\";");
        const auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = text,
            .origin = "header-token-test.cv",
        };
        const auto lexed = lex(source);
        if (!expect(lexed.diagnostics.empty())) {
            return;
        }
        const auto tokens = lexed.value.tokens();
        if (!expect_equal(tokens.size(), 6uz)) {
            return;
        }
        expect_equal(tokens[1].kind, TokenKind::CppAngleHeaderName);
        expect_equal(slice(text, tokens[1].span), std::string_view("<vendor/api.hpp>"));
        expect_equal(tokens[4].kind, TokenKind::CppQuoteHeaderName);
        expect_equal(slice(text, tokens[4].span), std::string_view("\"native/provider.hpp\""));

        check_lexical_error("import <>;");
        check_lexical_error("import \"\";");
        check_lexical_error("import <unterminated;");
        check_lexical_error("import \"unterminated;");
        check_lexical_error(std::string_view("import \"a\0b\";", 13));
        check_lexical_error(std::string_view("import <a\0b>;", 13));
    };

    "Lexer: numeric scanner exposes typed values and error facts"_test = [] static noexcept {
        const auto number = scan_numeric_literal("42u8 rest", 10u);
        if (!expect(number.has_value())) {
            return;
        }
        expect_equal(number->consumed, 4uz);
        const auto& integer = std::get<IntegerLiteralValue>(number->value);
        expect_equal(integer.magnitude, 42u);
        expect_equal(integer.suffix, NumericSuffix::U8);

        const auto invalid_number = scan_numeric_literal("0xg", 0u);
        if (!expect(!invalid_number.has_value())) {
            return;
        }
        expect_equal(invalid_number.error().consumed, 3uz);
        expect_equal(invalid_number.error().error_offset, 2uz);
        expect(invalid_number.error().has_base_prefix);
    };

    "Lexer: string scanner exposes typed values and error facts"_test = [] static noexcept {
        const auto string = scan_string_literal("\"a\\n\" rest");
        if (!expect(string.has_value())) {
            return;
        }
        expect_equal(string->consumed, 5uz);
        expect_equal(string->value.bytes, std::string_view("a\n"));

        const auto invalid_string = scan_string_literal("\"\\q\"");
        if (!expect(!invalid_string.has_value())) {
            return;
        }
        expect_equal(invalid_string.error().consumed, 4uz);
        expect_equal(invalid_string.error().error_offset, 1uz);
    };

    "Lexer: character scanner exposes typed values and error facts"_test = [] static noexcept {
        const auto character = scan_character_literal("'x' rest");
        if (!expect(character.has_value())) {
            return;
        }
        expect_equal(character->consumed, 3uz);
        expect(((character->value.scalar) == (U'x'))).note("character->value.scalar == U'x'");

        const auto invalid_character = scan_character_literal("''");
        if (!expect(!invalid_character.has_value())) {
            return;
        }
        expect_equal(invalid_character.error().consumed, 2uz);
        expect_equal(invalid_character.error().error_offset, 1uz);
    };

    "Lexer: numeric tokens preserve integer values and floating spellings"_test =
        [] static noexcept {
            static constexpr auto spellings = std::to_array<std::string_view>({
                "0",
                "42i32",
                "255u8",
                "1.5",
                "1e10",
                "1e-3f64",
                "7f32",
                "0xffu64",
                "0B101i8",
                "0o77usize",
            });
            each(spellings, std::identity {}, [](std::string_view spelling) static noexcept {
                const auto source = SourceView {
                    .source_id = SourceID::from_index(0),
                    .text = spelling,
                    .origin = "tokenize-test.cv",
                };
                const auto lexed = lex(source);
                if (!(expect(lexed.diagnostics.empty()).note("spelling = ", spelling))) {
                    return;
                }
                if (!(
                        expect_equal(lexed.value.tokens().size(), 1uz).note("spelling = ", spelling)
                    )) {
                    return;
                }
                const auto& token = lexed.value.tokens().front();
                expect_equal(token.kind, TokenKind::NumberLiteral).note("spelling = ", spelling);
                const auto& value = lexed.value.literal_value(0uz);
                const auto* integer = std::get_if<IntegerLiteralValue>(&value);
                const auto* floating = std::get_if<FloatingLiteralValue>(&value);
                if (!(expect((integer != nullptr || floating != nullptr))
                          .note("spelling = ", spelling))) {
                    return;
                }
                if (integer != nullptr) {
                    expect_equal(integer->conversion, NumericConversion::Exact)
                        .note("spelling = ", spelling);
                } else {
                    expect_equal(floating->spelling, slice(spelling, floating->value_span))
                        .note("spelling = ", spelling);
                }
            });
        };

    "Lexer: quoted tokens carry decoded values"_test = [] static noexcept {
        const auto string_source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = "\"a\\n\\u{4e09}\"",
            .origin = "tokenize-test.cv",
        };
        const auto string = lex(string_source);
        if (!expect(string.diagnostics.empty())) {
            return;
        }
        if (!expect_equal(string.value.tokens().size(), 1uz)) {
            return;
        }
        const auto& string_value = std::get<StringLiteralValue>(string.value.literal_value(0uz));
        expect_equal(string_value.bytes, std::string_view("a\n三"));

        const auto character_source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = "'\\u{1f600}'",
            .origin = "tokenize-test.cv",
        };
        const auto character = lex(character_source);
        if (!expect(character.diagnostics.empty())) {
            return;
        }
        const auto& character_value =
            std::get<CharacterLiteralValue>(character.value.literal_value(0uz));
        expect(((character_value.scalar) == (U'😀'))).note("character_value.scalar == U'😀'");

        const auto invalid_utf8_character = std::string("'\xff'", 3);
        const auto invalid_utf8_string = std::string("\"\xff\"", 3);
        check_lexical_error(invalid_utf8_character);
        check_lexical_error(invalid_utf8_string);
    };

    "Lexer: stored literal value associations follow token positions"_test = [] static noexcept {
        static constexpr auto text = std::string_view("let value = 42; false \"text\" true 'x'");
        const auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = text,
            .origin = "tokenize-test.cv",
        };
        const auto lexed = lex(source);
        if (!expect(lexed.diagnostics.empty())) {
            return;
        }
        const auto tokens = lexed.value.tokens();
        if (!expect_equal(tokens.size(), 9uz)) {
            return;
        }
        if (!expect_equal(tokens[3].kind, TokenKind::NumberLiteral)) {
            return;
        }
        if (!expect_equal(tokens[5].kind, TokenKind::False)) {
            return;
        }
        if (!expect_equal(tokens[6].kind, TokenKind::StringLiteral)) {
            return;
        }
        if (!expect_equal(tokens[7].kind, TokenKind::True)) {
            return;
        }
        if (!expect_equal(tokens[8].kind, TokenKind::CharLiteral)) {
            return;
        }

        const auto& number = std::get<IntegerLiteralValue>(lexed.value.literal_value(3uz));
        const auto& string = std::get<StringLiteralValue>(lexed.value.literal_value(6uz));
        const auto& character = std::get<CharacterLiteralValue>(lexed.value.literal_value(8uz));
        expect_equal(number.magnitude, 42u);
        expect_equal(string.bytes, std::string_view("text"));
        expect(((character.scalar) == (U'x'))).note("character.scalar == U'x'");
    };

    "Lexer: C strings share decoding and reject NUL at its source"_test = [] static noexcept {
        const auto valid =
            std::to_array<std::string_view>({R"(c"")", R"(c"hello\n")", R"(c"你好")"});
        each(valid, std::identity {}, [](const auto& text) static noexcept {
            check_token(text, TokenKind::CStringLiteral);
        });
        const auto zeros = std::to_array<std::string_view>({R"("a\0b")", R"("a\u{0}b")"});
        each(zeros, std::identity {}, [](std::string_view text) static noexcept {
            const auto result = scan_string_literal(text, StringLiteralKind::CString);
            if (!expect(!(result.has_value()))) {
                return;
            }
            expect_equal(result.error().error_offset, 2uz);
            expect(((result.error().error_length) == (text.size() - 4uz)))
                .note("result.error().error_length == text.size() - 4uz");
        });
        const auto invalid = std::to_array<std::string_view>({R"(c"\q")", R"(c"unterminated)"});
        each(invalid, std::identity {}, [](const auto& text) static noexcept {
            check_lexical_error(text);
        });
        const auto separated = std::array {
            TokenCase {"c", TokenKind::Identifier},
            TokenCase {R"("x")", TokenKind::StringLiteral}
        };
        check_token_sequence(R"(c "x")", separated);
    };

    "Lexer: raw boundaries preserve literal text and UTF-8"_test = [] static noexcept {
        struct Case final {
            std::string_view source;
            std::string_view expected;
        };

        const auto cases = std::to_array<Case>({
            {R"CV(r"C:\tools\bin\")CV", "C:\\tools\\bin\\"},
            {R"CV(r#"{"name": "我", "value": "\n"}"#)CV", R"({"name": "我", "value": "\n"})"},
            {R"CV(r##"a"#b"##)CV", "a\"#b"},
            {R"CV(r"{name} {{}} \u{0}")CV", R"({name} {{}} \u{0})"},
            {"r\"\"", ""},
        });
        each(cases, &Case::source, [](const Case& item) static noexcept {
            const auto result = scan_string_literal(item.source);
            if (!(expect(result.has_value()).note("item.source = ", item.source))) {
                return;
            }
            expect(result->consumed == item.source.size()).note("item.source = ", item.source);
            expect(result->value.bytes == item.expected).note("item.source = ", item.source);
            check_token(item.source, TokenKind::StringLiteral);
        });
        const auto separated = std::array {
            TokenCase {"r", TokenKind::Identifier},
            TokenCase {R"("x")", TokenKind::StringLiteral},
        };
        check_token_sequence(R"(r "x")", separated);
        const auto boundary = scan_string_literal(R"CV(r#"text"##)CV");
        if (!expect(boundary.has_value())) {
            return;
        }
        expect(boundary->consumed == 9uz);
        expect(boundary->value.bytes == "text");
        const auto malformed = std::to_array<std::string_view>({
            "r#",
            "r#\"x\"",
            "r##\"x\"#",
            "r\"a\nb\"",
            "r\"a\rb\"",
            "r\"\xff\"",
        });
        each(malformed, std::identity {}, [](const auto& input) static noexcept {
            check_lexical_error(input);
        });
    };

    "Lexer: multiline layout preserves relative whitespace before decoding"_test =
        [] static noexcept {
            struct Case final {
                std::string_view source;
                std::string_view expected;
            };

            const auto cases = std::to_array<Case>({
                {"\"\"\"\n    A\n      B\n\"\"\"", "A\n  B"},
                {"\"\"\"\n    A\n      B\n          \"\"\"", "A\n  B"},
                {"\"\"\"\n    A  \n      \n  \n    B\n\"\"\"", "A  \n  \n\nB"},
                {"\"\"\"\n\t A\n\t  B\n\"\"\"", "A\n B"},
                {"\"\"\"\n \tA\n\t B\n\"\"\"", " \tA\n\t B"},
                {"\"\"\"\n  A\n \t \n  B\n\"\"\"", "A\n\t \nB"},
                {"\"\"\"\n  A\nB\n\"\"\"", "  A\nB"},
                {"\"\"\"\r\n  A\r\n  B\r\n\"\"\"", "A\nB"},
                {"\"\"\"\n  \\tA\n  \\u{20}B\n\"\"\"", "\tA\n B"},
                {"\"\"\"\n  a\\0我\n\"\"\"", std::string_view("a\0我", 5)},
                {"\"\"\"\n  \\n\n  B\n\"\"\"", "\n\nB"},
                {"\"\"\"\n\n  A\n\n\"\"\"", "\nA\n"},
                {"\"\"\"\n\"\"\"", ""},
                {"\"\"\"\n  \t\"\"\"", ""},
                {"\"\"\"\n  \n\"\"\"", ""},
                {"\"\"\"\n  \n\t\n\"\"\"", "\n"},
                {"r\"\"\"\n  \\n{name}\n\"\"\"", R"(\n{name})"},
                {"r#\"\"\"\n  \"\"\" and \"#\n\"\"\"#", "\"\"\" and \"#"},
                {"\"\"\"\n  \\\"\"\"\n\"\"\"", "\"\"\""},
            });
            each(cases, &Case::source, [](const Case& item) static noexcept {
                const auto result = scan_string_literal(item.source);
                if (!(expect(result.has_value()).note("item.source = ", item.source))) {
                    return;
                }
                expect(result->consumed == item.source.size()).note("item.source = ", item.source);
                expect(result->value.bytes == item.expected).note("item.source = ", item.source);
                check_token(item.source, TokenKind::StringLiteral);
            });
            const auto invalid = std::to_array<std::string_view>({
                "\"\"\"inline\"\"\"",
                "\"\"\" \n\"\"\"",
                "\"\"\"\nA\"\"\"",
                "\"\"\"\nA",
                "\"\"\"\nA\rB\n\"\"\"",
                "\"\"\"\r\"\"\"",
                "\"\"\"\n\\\n\"\"\"",
                "\"\"\"\n\\q\n\"\"\"",
                "\"\"\"\n\xff\n\"\"\"",
                "r#\"\"\"\nA\n\"\"\"",
                "r\"\"\"\nA\"\"\"",
                "c\"\"\"\nA\n\"\"\"",
            });
            each(invalid, std::identity {}, [](const auto& input) static noexcept {
                check_lexical_error(input);
            });
        };
});

} // namespace
