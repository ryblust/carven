module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.graver.source;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.lex.token;
import :frontend.literal;
import :frontend.parse;
import :graver.source;
import :source.manager;
import :source.text;
import std;

namespace {

auto scan(std::string_view text) noexcept -> std::expected<graver::Source, Diagnostics> {
    return graver::Source::scan(
        SourceView {
            .source_id = SourceID::from_index(7),
            .text = text,
            .origin = "graver.cv",
        }
    );
}

// Reconstruct from classified ranges, not from Source::text(), so missing,
// overlapping or duplicated token/trivia bytes fail independently of the lexer.
auto check_coverage(const graver::Source& source, std::string_view expected) noexcept -> void {
    auto reconstructed = std::string();
    auto position = 0u;
    const auto append = [&](Span span) noexcept {
        CHECK(span.start() == position);
        CHECK_FALSE(span.empty());
        reconstructed += source.spelling(span);
        position = span.end();
    };
    const auto tokens = source.token_buffer().tokens();
    for (auto index = 0uz; index <= tokens.size(); ++index) {
        for (const auto trivia : source.trivia_before(index)) {
            append(trivia.span);
        }
        if (index < tokens.size()) {
            append(tokens[index].span);
        }
    }
    CHECK(position == expected.size());
    CHECK(reconstructed == expected);
}

}

TEST_CASE("Graver source: empty and tokenless files retain every byte") {
    const auto cases = std::to_array<std::string_view>({
        "",
        " \t",
        "\n\r\n\r",
        "// comment",
        " \t// 文件头\r\n\r\n// last\t ",
    });
    for (const auto input : cases) {
        CAPTURE(input);
        const auto result = scan(input);
        REQUIRE(result.has_value());
        CHECK(result->token_buffer().tokens().empty());
        check_coverage(*result, input);
    }
}

TEST_CASE("Graver source: gaps distinguish comments whitespace and line endings") {
    const auto input = std::string_view("// header\r\nlet\t x=1;  // 尾注释\r\n\n// end");
    const auto result = scan(input);
    REQUIRE(result.has_value());
    check_coverage(*result, input);
    const auto tokens = result->token_buffer().tokens();
    REQUIRE(tokens.size() == 5uz);
    const auto leading = result->trivia_before(0);
    REQUIRE(leading.size() == 2uz);
    CHECK(leading[0].kind == graver::TriviaKind::LineComment);
    CHECK(result->spelling(leading[0].span) == "// header");
    CHECK(leading[1].kind == graver::TriviaKind::LineEnding);
    CHECK(result->spelling(leading[1].span) == "\r\n");
    const auto between = result->trivia_before(1);
    REQUIRE(between.size() == 1uz);
    CHECK(between[0].kind == graver::TriviaKind::HorizontalWhitespace);
    CHECK(result->spelling(between[0].span) == "\t ");
    CHECK(result->trivia_before(2).empty());
    const auto trailing = result->trivia_before(tokens.size());
    REQUIRE(trailing.size() == 5uz);
    CHECK(trailing[1].kind == graver::TriviaKind::LineComment);
    CHECK(result->spelling(trailing[1].span) == "// 尾注释");
    CHECK(trailing[2].kind == graver::TriviaKind::LineEnding);
    CHECK(trailing[3].kind == graver::TriviaKind::LineEnding);
    CHECK(result->spelling(trailing[4].span) == "// end");
}

TEST_CASE("Graver source: comment positions include empty blocks and inline expressions") {
    const auto cases = std::to_array<std::string_view>({
        "fn f() {\n  // only body content\n}\n",
        "fn f() { let x = a // operand\n + b; // result\n}\n// eof",
        "fn f( // first\n x: i32, // parameter\n) {}",
    });
    for (const auto input : cases) {
        const auto result = scan(input);
        REQUIRE(result.has_value());
        check_coverage(*result, input);
    }
}

TEST_CASE("Graver source: raw literal spellings and compiler literal values both survive") {
    const auto input = std::string_view(R"(0xFFu32 "\u{41}// literal" c"// c string" '\n')");
    const auto result = scan(input);
    REQUIRE(result.has_value());
    check_coverage(*result, input);
    const auto& buffer = result->token_buffer();
    REQUIRE(buffer.tokens().size() == 4uz);
    CHECK(result->spelling(buffer.tokens()[0].span) == "0xFFu32");
    const auto* literal = std::get_if<StringLiteralValue>(&buffer.literal_value(1));
    REQUIRE(literal != nullptr);
    CHECK(literal->bytes == "A// literal");
    CHECK(result->spelling(buffer.tokens()[1].span) == R"("\u{41}// literal")");
    for (auto index = 0uz; index <= buffer.tokens().size(); ++index) {
        for (const auto trivia : result->trivia_before(index)) {
            CHECK(trivia.kind != graver::TriviaKind::LineComment);
        }
    }
}

TEST_CASE("Graver source: interpolation text is opaque but hole trivia is recovered") {
    const auto input = std::string_view("f\"literal // {{ }} { value // hole\r\n :0{ width }x}\"");
    const auto result = scan(input);
    REQUIRE(result.has_value());
    check_coverage(*result, input);
    auto comments = 0uz;
    const auto tokens = result->token_buffer().tokens();
    for (auto index = 0uz; index <= tokens.size(); ++index) {
        for (const auto trivia : result->trivia_before(index)) {
            if (trivia.kind == graver::TriviaKind::LineComment) {
                ++comments;
                CHECK(result->spelling(trivia.span) == "// hole");
            }
        }
    }
    CHECK(comments == 1uz);
}

TEST_CASE("Graver source: C++ fragments and header names remain opaque") {
    const auto input = std::string_view(
        "import <vector>;\r\nimport \"a//b.hpp\";\n"
        "#[cpp] ---\r\n// C++ comment\r\n/* block */ const char* s = \"//\";\r\n"
        "  ---\r\n// Carven comment\n"
    );
    const auto result = scan(input);
    REQUIRE(result.has_value());
    check_coverage(*result, input);
    const auto tokens = result->token_buffer().tokens();
    REQUIRE(tokens.back().kind == TokenKind::CppSourceFragment);
    CHECK(result->spelling(tokens.back().span).ends_with("  ---"));
    auto comments = 0uz;
    for (auto index = 0uz; index <= tokens.size(); ++index) {
        for (const auto trivia : result->trivia_before(index)) {
            if (trivia.kind == graver::TriviaKind::LineComment) {
                ++comments;
                CHECK(result->spelling(trivia.span) == "// Carven comment");
            }
        }
    }
    CHECK(comments == 1uz);
}

TEST_CASE("Graver source: block comment spelling is not invented as trivia") {
    const auto input = std::string_view("/* text */");
    const auto result = scan(input);
    REQUIRE(result.has_value());
    check_coverage(*result, input);
    CHECK(result->token_buffer().tokens().size() == 5uz);
    for (auto index = 0uz; index <= result->token_buffer().tokens().size(); ++index) {
        for (const auto trivia : result->trivia_before(index)) {
            CHECK(trivia.kind != graver::TriviaKind::LineComment);
        }
    }
}

TEST_CASE("Graver source: lexical errors deliver diagnostics instead of partial source") {
    const auto cases = std::to_array<std::string_view>({
        "@",
        "\"unterminated",
        "f\"{x",
        "// invalid UTF-8 \xff",
        "#[cpp] ---\nmissing fence",
    });
    for (const auto input : cases) {
        CAPTURE(input);
        const auto result = scan(input);
        REQUIRE_FALSE(result.has_value());
        REQUIRE_FALSE(result.error().empty());
        CHECK(result.error().front().finding.severity == DiagnosticSeverity::Error);
        REQUIRE(result.error().front().attachment.primary.has_value());
        CHECK(result.error().front().attachment.primary->span.source_id == SourceID::from_index(7));
    }
    auto nested = std::string();
    for (auto index = 0uz; index < 513uz; ++index) {
        nested += "f\"{";
    }
    nested += '0';
    for (auto index = 0uz; index < 513uz; ++index) {
        nested += "}\"";
    }
    const auto rejected = scan(nested);
    REQUIRE_FALSE(rejected.has_value());
    CHECK_FALSE(rejected.error().empty());
}

TEST_CASE("Graver source: owned bytes outlive input and remain valid after moving") {
    auto result = []() static noexcept {
        auto temporary = std::string("let x = 42; // owned\n");
        auto scanned = scan(temporary);
        temporary.assign(temporary.size(), '?');
        return scanned;
    }();
    REQUIRE(result.has_value());
    const auto moved = std::move(*result);
    check_coverage(moved, "let x = 42; // owned\n");
    CHECK(moved.text() == "let x = 42; // owned\n");
}

TEST_CASE("Graver source: compiler tokens feed the single-file parser without resolving imports") {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual(
        "format.cv",
        "import missing::dependency using *;\nfn f() { // body\n}\n"
    );
    REQUIRE(source_id.has_value());
    const auto result = graver::Source::scan(sources.view(*source_id));
    REQUIRE(result.has_value());
    CHECK(result->token_buffer().source_id() == *source_id);
    const auto parsed = parse(sources, result->token_buffer());
    CHECK(parsed.has_value());

    const auto incomplete_id = sources.append_virtual("incomplete.cv", "fn f( // unfinished\n");
    REQUIRE(incomplete_id.has_value());
    const auto incomplete = graver::Source::scan(sources.view(*incomplete_id));
    REQUIRE(incomplete.has_value());
    check_coverage(*incomplete, sources.view(*incomplete_id).text);
    CHECK_FALSE(parse(sources, incomplete->token_buffer()).has_value());
}
