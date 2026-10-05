module carven:test.internal.frontend.lex.tokens;

import :frontend.lex;
import :frontend.lex.token;
import :source.text;
import :test.harness.framework;
import :test.internal.frontend.lex.fixture;
import std;

static_assert(!std::copy_constructible<TokenBuffer>);
static_assert(std::movable<TokenBuffer>);

namespace {

const TestSuite suite([] static noexcept {
    "Lexer: every reserved spelling has its grammar token"_test = [] static noexcept {
        static constexpr auto cases = std::to_array<TokenCase>({
            {.spelling = "as", .kind = TokenKind::As},
            {.spelling = "break", .kind = TokenKind::Break},
            {.spelling = "const", .kind = TokenKind::Const},
            {.spelling = "continue", .kind = TokenKind::Continue},
            {.spelling = "else", .kind = TokenKind::Else},
            {.spelling = "enum", .kind = TokenKind::Enum},
            {.spelling = "export", .kind = TokenKind::Export},
            {.spelling = "false", .kind = TokenKind::False},
            {.spelling = "fn", .kind = TokenKind::Fn},
            {.spelling = "for", .kind = TokenKind::For},
            {.spelling = "if", .kind = TokenKind::If},
            {.spelling = "import", .kind = TokenKind::Import},
            {.spelling = "in", .kind = TokenKind::In},
            {.spelling = "is", .kind = TokenKind::Is},
            {.spelling = "let", .kind = TokenKind::Let},
            {.spelling = "match", .kind = TokenKind::Match},
            {.spelling = "nullptr", .kind = TokenKind::Nullptr},
            {.spelling = "private", .kind = TokenKind::Private},
            {.spelling = "return", .kind = TokenKind::Return},
            {.spelling = "rethrow", .kind = TokenKind::Rethrow},
            {.spelling = "struct", .kind = TokenKind::Struct},
            {.spelling = "test", .kind = TokenKind::Test},
            {.spelling = "throw", .kind = TokenKind::Throw},
            {.spelling = "true", .kind = TokenKind::True},
            {.spelling = "try", .kind = TokenKind::Try},
            {.spelling = "catch", .kind = TokenKind::Catch},
            {.spelling = "using", .kind = TokenKind::Using},
            {.spelling = "var", .kind = TokenKind::Var},
            {.spelling = "while", .kind = TokenKind::While},
        });
        check_tokens(cases);
    };

    "Lexer: identifiers are ASCII"_test = [] static noexcept {
        static constexpr auto identifiers = std::to_array<std::string_view>({
            "name",
            "_",
            "_private",
            "__value",
            "__carven_temporary",
            "value_42",
            "imported",
            "new",
            "delete",
        });
        each(identifiers, std::identity {}, [](const auto& spelling) static noexcept {
            check_token(spelling, TokenKind::Identifier);
        });

        check_lexical_error("变量");
        check_lexical_error(std::string_view("\xff", 1));
    };

    "Lexer: every source-text ingress validates UTF-8"_test = [] static noexcept {
        static constexpr auto malformed = [](std::string prefix,
                                             const std::string& suffix) static noexcept {
            prefix.push_back(static_cast<char>(0xff));
            prefix += suffix;
            return prefix;
        };

        const auto ordinary = malformed({}, {});
        const auto comment = malformed("// ", "\nlet value = 1;");
        const auto quoted_header = malformed("import \"vendor/", "\";");
        const auto angle_header = malformed("import <vendor/", ">;");
        const auto source_fragment = malformed("#[cpp] ---\nauto value = \"", "\";\n---");
        for (const auto& text : {ordinary, comment, quoted_header, angle_header, source_fragment}) {
            const auto source = SourceView {
                .source_id = SourceID::from_index(0),
                .text = text,
                .origin = "utf8-ingress-test.cv",
            };
            const auto result = lex(source);
            expect(
                std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) static noexcept {
                    return diagnostic.finding.message == "invalid UTF-8 encoding";
                })
            ).note("text = ", text);
        }
    };

    "Lexer: throw syntax reserves singular keywords only"_test = [] static noexcept {
        check_token("throws", TokenKind::Identifier);
        check_token_sequence(
            "??",
            std::to_array<TokenCase>({
                {.spelling = "?", .kind = TokenKind::Question},
                {.spelling = "?", .kind = TokenKind::Question},
            })
        );
    };

    "Lexer: punctuators use maximal munch"_test = [] static noexcept {
        static constexpr auto cases = std::to_array<TokenCase>({
            {.spelling = "(", .kind = TokenKind::LeftParen},
            {.spelling = ")", .kind = TokenKind::RightParen},
            {.spelling = "[", .kind = TokenKind::LeftBracket},
            {.spelling = "]", .kind = TokenKind::RightBracket},
            {.spelling = "{", .kind = TokenKind::LeftBrace},
            {.spelling = "}", .kind = TokenKind::RightBrace},
            {.spelling = ",", .kind = TokenKind::Comma},
            {.spelling = ".", .kind = TokenKind::Dot},
            {.spelling = "..", .kind = TokenKind::DotDot},
            {.spelling = ":", .kind = TokenKind::Colon},
            {.spelling = ";", .kind = TokenKind::Semicolon},
            {.spelling = "+", .kind = TokenKind::Plus},
            {.spelling = "-", .kind = TokenKind::Minus},
            {.spelling = "*", .kind = TokenKind::Star},
            {.spelling = "/", .kind = TokenKind::Slash},
            {.spelling = "%", .kind = TokenKind::Percent},
            {.spelling = "!", .kind = TokenKind::Bang},
            {.spelling = "=", .kind = TokenKind::Equal},
            {.spelling = "<", .kind = TokenKind::Less},
            {.spelling = ">", .kind = TokenKind::Greater},
            {.spelling = "&", .kind = TokenKind::Ampersand},
            {.spelling = "|", .kind = TokenKind::Pipe},
            {.spelling = "^", .kind = TokenKind::Caret},
            {.spelling = "~", .kind = TokenKind::Tilde},
            {.spelling = "+=", .kind = TokenKind::PlusEqual},
            {.spelling = "-=", .kind = TokenKind::MinusEqual},
            {.spelling = "*=", .kind = TokenKind::StarEqual},
            {.spelling = "/=", .kind = TokenKind::SlashEqual},
            {.spelling = "%=", .kind = TokenKind::PercentEqual},
            {.spelling = "!=", .kind = TokenKind::BangEqual},
            {.spelling = "==", .kind = TokenKind::EqualEqual},
            {.spelling = "<=", .kind = TokenKind::LessEqual},
            {.spelling = ">=", .kind = TokenKind::GreaterEqual},
            {.spelling = "++", .kind = TokenKind::PlusPlus},
            {.spelling = "--", .kind = TokenKind::MinusMinus},
            {.spelling = "&&", .kind = TokenKind::AmpersandAmpersand},
            {.spelling = "||", .kind = TokenKind::PipePipe},
            {.spelling = "<<", .kind = TokenKind::LeftShift},
            {.spelling = ">>", .kind = TokenKind::RightShift},
            {.spelling = "&=", .kind = TokenKind::AmpersandEqual},
            {.spelling = "|=", .kind = TokenKind::PipeEqual},
            {.spelling = "^=", .kind = TokenKind::CaretEqual},
            {.spelling = "<<=", .kind = TokenKind::LeftShiftEqual},
            {.spelling = ">>=", .kind = TokenKind::RightShiftEqual},
            {.spelling = "->", .kind = TokenKind::Arrow},
            {.spelling = "=>", .kind = TokenKind::FatArrow},
            {.spelling = "::", .kind = TokenKind::ColonColon},
        });
        check_tokens(cases);
    };

    "Lexer: range punctuation preserves numbers and member access"_test = [] static noexcept {
        static constexpr auto integer_range = std::to_array<TokenCase>({
            {.spelling = "1", .kind = TokenKind::NumberLiteral},
            {.spelling = "..", .kind = TokenKind::DotDot},
            {.spelling = "10", .kind = TokenKind::NumberLiteral},
        });
        check_token_sequence("1..10", integer_range);
        check_token_sequence("1 .. 10", integer_range);

        check_token_sequence(
            "1 . . 10",
            std::to_array<TokenCase>({
                {.spelling = "1", .kind = TokenKind::NumberLiteral},
                {.spelling = ".", .kind = TokenKind::Dot},
                {.spelling = ".", .kind = TokenKind::Dot},
                {.spelling = "10", .kind = TokenKind::NumberLiteral},
            })
        );
        check_token_sequence(
            "1.5..10",
            std::to_array<TokenCase>({
                {.spelling = "1.5", .kind = TokenKind::NumberLiteral},
                {.spelling = "..", .kind = TokenKind::DotDot},
                {.spelling = "10", .kind = TokenKind::NumberLiteral},
            })
        );
        check_token_sequence(
            "value.member",
            std::to_array<TokenCase>({
                {.spelling = "value", .kind = TokenKind::Identifier},
                {.spelling = ".", .kind = TokenKind::Dot},
                {.spelling = "member", .kind = TokenKind::Identifier},
            })
        );
    };

    "Lexer: discarded text does not disturb source spans"_test = [] static noexcept {
        static constexpr auto text = std::string_view(" \t// first\r\nlet\nvalue");
        const auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = text,
            .origin = "tokenize-test.cv",
        };
        const auto result = lex(source);
        if (!expect(result.diagnostics.empty())) {
            return;
        }
        const auto tokens = result.value.tokens();
        if (!expect_equal(tokens.size(), 2uz)) {
            return;
        }
        expect_equal(tokens[0].kind, TokenKind::Let);
        expect_equal(slice(text, tokens[0].span), std::string_view("let"));
        expect_equal(tokens[1].kind, TokenKind::Identifier);
        expect_equal(slice(text, tokens[1].span), std::string_view("value"));
    };

    "Lexer: multiple lexical errors do not require an end sentinel"_test = [] static noexcept {
        static constexpr auto text = std::string_view("@ let value = 1; $");
        const auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = text,
            .origin = "tokenize-test.cv",
        };
        const auto result = lex(source);

        expect_equal(result.diagnostics.size(), 2uz);
        expect_equal(
            result.diagnostics[0].finding.message,
            std::string_view("unknown source character")
        );
        if (!expect(result.diagnostics[0].attachment.primary.has_value())) {
            return;
        }
        expect_equal(result.diagnostics[0].attachment.primary->span.span.start(), 0u);
        expect_equal(result.diagnostics[0].attachment.primary->span.span.end(), 1u);
        expect_equal(
            result.diagnostics[1].finding.message,
            std::string_view("unknown source character")
        );
        if (!expect(result.diagnostics[1].attachment.primary.has_value())) {
            return;
        }
        expect_equal(result.diagnostics[1].attachment.primary->span.span.start(), 17u);
        expect_equal(result.diagnostics[1].attachment.primary->span.span.end(), 18u);
        const auto tokens = result.value.tokens();
        if (!expect_equal(tokens.size(), 7uz)) {
            return;
        }
        expect_equal(tokens.front().kind, TokenKind::Invalid);
        expect_equal(tokens.back().kind, TokenKind::Invalid);
        expect_equal(slice(text, tokens[1].span), std::string_view("let"));
    };
});

} // namespace
