module carven:test.graver.source;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.lex.token;
import :frontend.literal;
import :frontend.parse;
import :graver.source;
import :source.manager;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

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
        ct::expect_equal(span.start(), position);
        ct::expect(!(span.empty()));
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
    ct::expect_equal(position, expected.size());
    ct::expect_equal(reconstructed, expected);
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Graver source: empty and tokenless files retain every byte", [] static noexcept {
        const auto cases = std::to_array<std::pair<std::string_view, std::string_view>>({
            {"Empty", ""},
            {"Horizontal whitespace", " \t"},
            {"Line endings", "\n\r\n\r"},
            {"Comment", "// comment"},
            {"Multilingual comments", " \t// 文件头\r\n\r\n// last\t "},
        });
        ct::each(
            cases,
            [](const auto& entry) static noexcept { return entry.first; },
            [&](const auto& entry) noexcept {
                const auto input = entry.second;
                const auto result = scan(input);
                if (!ct::expect(result.has_value())) {
                    return;
                }
                ct::expect(result->token_buffer().tokens().empty());
                check_coverage(*result, input);
            }
        );
    });

    ct::test(
        "Graver source: gaps distinguish comments whitespace and line endings",
        [] static noexcept {
            const auto input = std::string_view("// header\r\nlet\t x=1;  // 尾注释\r\n\n// end");
            const auto result = scan(input);
            if (!ct::expect(result.has_value())) {
                return;
            }
            check_coverage(*result, input);
            const auto tokens = result->token_buffer().tokens();
            if (!ct::expect_equal(tokens.size(), 5uz)) {
                return;
            }
            const auto leading = result->trivia_before(0);
            if (!ct::expect_equal(leading.size(), 2uz)) {
                return;
            }
            ct::expect_equal(leading[0].kind, graver::TriviaKind::LineComment);
            ct::expect_equal(result->spelling(leading[0].span), std::string_view("// header"));
            ct::expect_equal(leading[1].kind, graver::TriviaKind::LineEnding);
            ct::expect_equal(result->spelling(leading[1].span), std::string_view("\r\n"));
            const auto between = result->trivia_before(1);
            if (!ct::expect_equal(between.size(), 1uz)) {
                return;
            }
            ct::expect_equal(between[0].kind, graver::TriviaKind::HorizontalWhitespace);
            ct::expect_equal(result->spelling(between[0].span), std::string_view("\t "));
            ct::expect(result->trivia_before(2).empty());
            const auto trailing = result->trivia_before(tokens.size());
            if (!ct::expect_equal(trailing.size(), 5uz)) {
                return;
            }
            ct::expect_equal(trailing[1].kind, graver::TriviaKind::LineComment);
            ct::expect_equal(result->spelling(trailing[1].span), std::string_view("// 尾注释"));
            ct::expect_equal(trailing[2].kind, graver::TriviaKind::LineEnding);
            ct::expect_equal(trailing[3].kind, graver::TriviaKind::LineEnding);
            ct::expect_equal(result->spelling(trailing[4].span), std::string_view("// end"));
        }
    );

    ct::test(
        "Graver source: comment positions include empty blocks and inline expressions",
        [] static noexcept {
            const auto cases = std::to_array<std::pair<std::string_view, std::string_view>>({
                {"Block-only comment", "fn f() {\n  // only body content\n}\n"},
                {"Inline expression comment",
                 "fn f() { let x = a // operand\n + b; // result\n}\n// eof"},
                {"Parameter comment", "fn f( // first\n x: i32, // parameter\n) {}"},
            });
            ct::each(
                cases,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    const auto input = entry.second;
                    const auto result = scan(input);
                    if (!ct::expect(result.has_value())) {
                        return;
                    }
                    check_coverage(*result, input);
                }
            );
        }
    );

    ct::test(
        "Graver source: raw literal spellings and compiler literal values both survive",
        [] static noexcept {
            const auto input =
                std::string_view(R"(0xFFu32 "\u{41}// literal" c"// c string" '\n')");
            const auto result = scan(input);
            if (!ct::expect(result.has_value())) {
                return;
            }
            check_coverage(*result, input);
            const auto& buffer = result->token_buffer();
            if (!ct::expect_equal(buffer.tokens().size(), 4uz)) {
                return;
            }
            ct::expect_equal(
                result->spelling(buffer.tokens()[0].span),
                std::string_view("0xFFu32")
            );
            const auto* literal = std::get_if<StringLiteralValue>(&buffer.literal_value(1));
            if (!ct::expect(literal != nullptr)) {
                return;
            }
            ct::expect_equal(literal->bytes, std::string_view("A// literal"));
            ct::expect_equal(
                result->spelling(buffer.tokens()[1].span),
                std::string_view(R"("\u{41}// literal")")
            );
            for (auto index = 0uz; index <= buffer.tokens().size(); ++index) {
                for (const auto trivia : result->trivia_before(index)) {
                    ct::expect_not_equal(trivia.kind, graver::TriviaKind::LineComment);
                }
            }
        }
    );

    ct::test(
        "Graver source: interpolation text is opaque but hole trivia is recovered",
        [] static noexcept {
            const auto input =
                std::string_view("f\"literal // {{ }} { value // hole\r\n :0{ width }x}\"");
            const auto result = scan(input);
            if (!ct::expect(result.has_value())) {
                return;
            }
            check_coverage(*result, input);
            auto comments = 0uz;
            const auto tokens = result->token_buffer().tokens();
            for (auto index = 0uz; index <= tokens.size(); ++index) {
                for (const auto trivia : result->trivia_before(index)) {
                    if (trivia.kind == graver::TriviaKind::LineComment) {
                        ++comments;
                        ct::expect_equal(
                            result->spelling(trivia.span),
                            std::string_view("// hole")
                        );
                    }
                }
            }
            ct::expect_equal(comments, 1uz);
        }
    );

    ct::test("Graver source: C++ fragments and header names remain opaque", [] static noexcept {
        const auto input = std::string_view(
            "import <vector>;\r\nimport \"a//b.hpp\";\n"
            "#[cpp] ---\r\n// C++ comment\r\n/* block */ const char* s = \"//\";\r\n"
            "  ---\r\n// Carven comment\n"
        );
        const auto result = scan(input);
        if (!ct::expect(result.has_value())) {
            return;
        }
        check_coverage(*result, input);
        const auto tokens = result->token_buffer().tokens();
        if (!ct::expect_equal(tokens.back().kind, TokenKind::CppSourceFragment)) {
            return;
        }
        ct::expect(result->spelling(tokens.back().span).ends_with("  ---"));
        auto comments = 0uz;
        for (auto index = 0uz; index <= tokens.size(); ++index) {
            for (const auto trivia : result->trivia_before(index)) {
                if (trivia.kind == graver::TriviaKind::LineComment) {
                    ++comments;
                    ct::expect_equal(
                        result->spelling(trivia.span),
                        std::string_view("// Carven comment")
                    );
                }
            }
        }
        ct::expect_equal(comments, 1uz);
    });

    ct::test("Graver source: block comment spelling is not invented as trivia", [] static noexcept {
        const auto input = std::string_view("/* text */");
        const auto result = scan(input);
        if (!ct::expect(result.has_value())) {
            return;
        }
        check_coverage(*result, input);
        ct::expect_equal(result->token_buffer().tokens().size(), 5uz);
        for (auto index = 0uz; index <= result->token_buffer().tokens().size(); ++index) {
            for (const auto trivia : result->trivia_before(index)) {
                ct::expect_not_equal(trivia.kind, graver::TriviaKind::LineComment);
            }
        }
    });

    ct::test(
        "Graver source: lexical errors deliver diagnostics instead of partial source",
        [] static noexcept {
            const auto result = scan("@");
            if (!ct::expect(!result.has_value())) {
                return;
            }
            if (!ct::expect(!result.error().empty())) {
                return;
            }
            ct::expect_equal(result.error().front().finding.severity, DiagnosticSeverity::Error);
            if (!ct::expect(result.error().front().attachment.primary.has_value())) {
                return;
            }
            ct::expect(
                result.error().front().attachment.primary->span.source_id == SourceID::from_index(7)
            );
        }
    );

    ct::test(
        "Graver source: owned bytes outlive input and remain valid after moving",
        [] static noexcept {
            auto result = []() static noexcept {
                auto temporary = std::string("let x = 42; // owned\n");
                auto scanned = scan(temporary);
                temporary.assign(temporary.size(), '?');
                return scanned;
            }();
            if (!ct::expect(result.has_value())) {
                return;
            }
            const auto moved = std::move(*result);
            check_coverage(moved, "let x = 42; // owned\n");
            ct::expect_equal(moved.text(), std::string_view("let x = 42; // owned\n"));
        }
    );

    ct::test(
        "Graver source: compiler tokens feed the single-file parser without resolving imports",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = sources.append_virtual(
                "format.cv",
                "import missing::dependency using *;\nfn f() { // body\n}\n"
            );
            if (!ct::expect(source_id.has_value())) {
                return;
            }
            const auto result = graver::Source::scan(sources.view(*source_id));
            if (!ct::expect(result.has_value())) {
                return;
            }
            ct::expect(result->token_buffer().source_id() == *source_id);
            const auto parsed = parse(sources, result->token_buffer());
            ct::expect(parsed.has_value());

            const auto incomplete_id =
                sources.append_virtual("incomplete.cv", "fn f( // unfinished\n");
            if (!ct::expect(incomplete_id.has_value())) {
                return;
            }
            const auto incomplete = graver::Source::scan(sources.view(*incomplete_id));
            if (!ct::expect(incomplete.has_value())) {
                return;
            }
            check_coverage(*incomplete, sources.view(*incomplete_id).text);
            ct::expect(!(parse(sources, incomplete->token_buffer()).has_value()));
        }
    );
});

} // namespace
