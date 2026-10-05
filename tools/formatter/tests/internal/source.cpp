module carven:test.formatter.source;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :formatter.source;
import :frontend.lex.token;
import :frontend.literal;
import :frontend.parse;
import :source.manager;
import :source.text;
import :test.harness.framework;
import std;

namespace {

auto scan(std::string_view text) noexcept -> std::expected<FormattingSource, Diagnostics> {
    return FormattingSource::scan(
        SourceView {
            .source_id = SourceID::from_index(7),
            .text = text,
            .origin = "formatter.cv",
        }
    );
}

// Reconstruct from classified ranges, not from FormattingSource::text(), so missing,
// overlapping or duplicated token/trivia bytes fail independently of the lexer.
auto check_coverage(const FormattingSource& source, std::string_view expected) noexcept -> void {
    auto reconstructed = std::string();
    auto position = 0u;
    const auto append = [&](Span span) noexcept {
        expect_equal(span.start(), position);
        expect(!(span.empty()));
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
    expect_equal(position, expected.size());
    expect_equal(reconstructed, expected);
}

const TestSuite suite([] static noexcept {
    "Formatter source: empty and tokenless files retain every byte"_test = [] static noexcept {
        const auto cases = std::to_array<std::pair<std::string_view, std::string_view>>({
            {"Empty", ""},
            {"Horizontal whitespace", " \t"},
            {"Line endings", "\n\r\n\r"},
            {"Comment", "// comment"},
            {"Multilingual comments", " \t// 文件头\r\n\r\n// last\t "},
        });
        each(
            cases,
            [](const auto& entry) static noexcept { return entry.first; },
            [&](const auto& entry) noexcept {
                const auto input = entry.second;
                const auto result = scan(input);
                if (!expect(result.has_value())) {
                    return;
                }
                expect(result->token_buffer().tokens().empty());
                check_coverage(*result, input);
            }
        );
    };

    "Formatter source: gaps distinguish comments whitespace and line endings"_test =
        [] static noexcept {
            const auto input = std::string_view("// header\r\nlet\t x=1;  // 尾注释\r\n\n// end");
            const auto result = scan(input);
            if (!expect(result.has_value())) {
                return;
            }
            check_coverage(*result, input);
            const auto tokens = result->token_buffer().tokens();
            if (!expect_equal(tokens.size(), 5uz)) {
                return;
            }
            const auto leading = result->trivia_before(0);
            if (!expect_equal(leading.size(), 2uz)) {
                return;
            }
            expect_equal(leading[0].kind, SourceTriviaKind::LineComment);
            expect_equal(result->spelling(leading[0].span), std::string_view("// header"));
            expect_equal(leading[1].kind, SourceTriviaKind::LineEnding);
            expect_equal(result->spelling(leading[1].span), std::string_view("\r\n"));
            const auto between = result->trivia_before(1);
            if (!expect_equal(between.size(), 1uz)) {
                return;
            }
            expect_equal(between[0].kind, SourceTriviaKind::HorizontalWhitespace);
            expect_equal(result->spelling(between[0].span), std::string_view("\t "));
            expect(result->trivia_before(2).empty());
            const auto trailing = result->trivia_before(tokens.size());
            if (!expect_equal(trailing.size(), 5uz)) {
                return;
            }
            expect_equal(trailing[1].kind, SourceTriviaKind::LineComment);
            expect_equal(result->spelling(trailing[1].span), std::string_view("// 尾注释"));
            expect_equal(trailing[2].kind, SourceTriviaKind::LineEnding);
            expect_equal(trailing[3].kind, SourceTriviaKind::LineEnding);
            expect_equal(result->spelling(trailing[4].span), std::string_view("// end"));
        };

    "Formatter source: comment positions include empty blocks and inline expressions"_test =
        [] static noexcept {
            const auto cases = std::to_array<std::pair<std::string_view, std::string_view>>({
                {"Block-only comment", "fn f() {\n  // only body content\n}\n"},
                {"Inline expression comment",
                 "fn f() { let x = a // operand\n + b; // result\n}\n// eof"},
                {"Parameter comment", "fn f( // first\n x: i32, // parameter\n) {}"},
            });
            each(
                cases,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    const auto input = entry.second;
                    const auto result = scan(input);
                    if (!expect(result.has_value())) {
                        return;
                    }
                    check_coverage(*result, input);
                }
            );
        };

    "Formatter source: raw literal spellings and compiler literal values both survive"_test =
        [] static noexcept {
            const auto input =
                std::string_view(R"(0xFFu32 "\u{41}// literal" c"// c string" '\n')");
            const auto result = scan(input);
            if (!expect(result.has_value())) {
                return;
            }
            check_coverage(*result, input);
            const auto& buffer = result->token_buffer();
            if (!expect_equal(buffer.tokens().size(), 4uz)) {
                return;
            }
            expect_equal(result->spelling(buffer.tokens()[0].span), std::string_view("0xFFu32"));
            const auto* literal = std::get_if<StringLiteralValue>(&buffer.literal_value(1));
            if (!expect(literal != nullptr)) {
                return;
            }
            expect_equal(literal->bytes, std::string_view("A// literal"));
            expect_equal(
                result->spelling(buffer.tokens()[1].span),
                std::string_view(R"("\u{41}// literal")")
            );
            for (auto index = 0uz; index <= buffer.tokens().size(); ++index) {
                for (const auto trivia : result->trivia_before(index)) {
                    expect_not_equal(trivia.kind, SourceTriviaKind::LineComment);
                }
            }
        };

    "Formatter source: interpolation text is opaque but hole trivia is recovered"_test =
        [] static noexcept {
            const auto input =
                std::string_view("f\"literal // {{ }} { value // hole\r\n :0{ width }x}\"");
            const auto result = scan(input);
            if (!expect(result.has_value())) {
                return;
            }
            check_coverage(*result, input);
            auto comments = 0uz;
            const auto tokens = result->token_buffer().tokens();
            for (auto index = 0uz; index <= tokens.size(); ++index) {
                for (const auto trivia : result->trivia_before(index)) {
                    if (trivia.kind == SourceTriviaKind::LineComment) {
                        ++comments;
                        expect_equal(result->spelling(trivia.span), std::string_view("// hole"));
                    }
                }
            }
            expect_equal(comments, 1uz);
        };

    "Formatter source: C++ fragments and header names remain opaque"_test = [] static noexcept {
        const auto input = std::string_view(
            "import <vector>;\r\nimport \"a//b.hpp\";\n"
            "#[cpp] ---\r\n// C++ comment\r\n/* block */ const char* s = \"//\";\r\n"
            "  ---\r\n// Carven comment\n"
        );
        const auto result = scan(input);
        if (!expect(result.has_value())) {
            return;
        }
        check_coverage(*result, input);
        const auto tokens = result->token_buffer().tokens();
        if (!expect_equal(tokens.back().kind, TokenKind::CppSourceFragment)) {
            return;
        }
        expect(result->spelling(tokens.back().span).ends_with("  ---"));
        auto comments = 0uz;
        for (auto index = 0uz; index <= tokens.size(); ++index) {
            for (const auto trivia : result->trivia_before(index)) {
                if (trivia.kind == SourceTriviaKind::LineComment) {
                    ++comments;
                    expect_equal(
                        result->spelling(trivia.span),
                        std::string_view("// Carven comment")
                    );
                }
            }
        }
        expect_equal(comments, 1uz);
    };

    "Formatter source: block comment spelling is not invented as trivia"_test = [] static noexcept {
        const auto input = std::string_view("/* text */");
        const auto result = scan(input);
        if (!expect(result.has_value())) {
            return;
        }
        check_coverage(*result, input);
        expect_equal(result->token_buffer().tokens().size(), 5uz);
        for (auto index = 0uz; index <= result->token_buffer().tokens().size(); ++index) {
            for (const auto trivia : result->trivia_before(index)) {
                expect_not_equal(trivia.kind, SourceTriviaKind::LineComment);
            }
        }
    };

    "Formatter source: lexical errors deliver diagnostics instead of partial source"_test =
        [] static noexcept {
            const auto result = scan("@");
            if (!expect(!result.has_value())) {
                return;
            }
            if (!expect(!result.error().empty())) {
                return;
            }
            expect_equal(result.error().front().finding.severity, DiagnosticSeverity::Error);
            if (!expect(result.error().front().attachment.primary.has_value())) {
                return;
            }
            expect(
                result.error().front().attachment.primary->span.source_id == SourceID::from_index(7)
            );
        };

    "Formatter source: owned bytes outlive input and remain valid after moving"_test =
        [] static noexcept {
            auto result = []() static noexcept {
                auto temporary = std::string("let x = 42; // owned\n");
                auto scanned = scan(temporary);
                temporary.assign(temporary.size(), '?');
                return scanned;
            }();
            if (!expect(result.has_value())) {
                return;
            }
            const auto moved = std::move(*result);
            check_coverage(moved, "let x = 42; // owned\n");
            expect_equal(moved.text(), std::string_view("let x = 42; // owned\n"));
        };

    "Formatter source: compiler tokens feed the single-file parser without resolving imports"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = sources.append_virtual(
                "format.cv",
                "import missing::dependency using *;\nfn f() { // body\n}\n"
            );
            if (!expect(source_id.has_value())) {
                return;
            }
            const auto result = FormattingSource::scan(sources.view(*source_id));
            if (!expect(result.has_value())) {
                return;
            }
            expect(result->token_buffer().source_id() == *source_id);
            const auto parsed = parse(sources, result->token_buffer());
            expect(parsed.has_value());

            const auto incomplete_id =
                sources.append_virtual("incomplete.cv", "fn f( // unfinished\n");
            if (!expect(incomplete_id.has_value())) {
                return;
            }
            const auto incomplete = FormattingSource::scan(sources.view(*incomplete_id));
            if (!expect(incomplete.has_value())) {
                return;
            }
            check_coverage(*incomplete, sources.view(*incomplete_id).text);
            expect(!(parse(sources, incomplete->token_buffer()).has_value()));
        };
});

} // namespace
