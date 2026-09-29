module carven:test.internal.frontend.lex.interpolation;

import :test.harness.framework;
import :test.internal.frontend.lex.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Lexer: interpolation prefix and scoped delimiters", [] static noexcept {
        const auto expected = std::to_array<TokenCase>({
            {"f", TokenKind::Identifier},
            {"\"x\"", TokenKind::StringLiteral},
            {"f\"", TokenKind::InterpolationStart},
            {"a{{b}}", TokenKind::InterpolationText},
            {"{", TokenKind::InterpolationOpen},
            {"x", TokenKind::Identifier},
            {"::", TokenKind::ColonColon},
            {"y", TokenKind::Identifier},
            {":", TokenKind::InterpolationSpec},
            {"0", TokenKind::InterpolationText},
            {"{", TokenKind::InterpolationOpen},
            {"w", TokenKind::Identifier},
            {"}", TokenKind::InterpolationClose},
            {"x", TokenKind::InterpolationText},
            {"}", TokenKind::InterpolationClose},
            {"\"", TokenKind::InterpolationEnd},
        });
        check_token_sequence(R"(f "x" f"a{{b}}{x::y:0{w}x}")", expected);
    });

    ct::test("Lexer: interpolation escapes stay text and retain source spans", [] static noexcept {
        const auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = R"(f"\u{7b}\0\u{7d}{{}}")",
            .origin = "interpolation.cv"
        };
        const auto result = lex(source);
        if (!ct::expect(result.diagnostics.empty())) {
            return;
        }
        if (!ct::expect(result.value.tokens().size() == 3uz)) {
            return;
        }
        const auto* text = std::get_if<InterpolationTextValue>(&result.value.literal_value(1));
        if (!ct::expect(text != nullptr)) {
            return;
        }
        ct::expect(text->bytes == std::string_view("{\0}{}", 5));
        ct::expect(slice(source.text, result.value.tokens()[1].span) == R"(\u{7b}\0\u{7d}{{}})");
        const auto invalid = std::array {
            R"(f"}")",
            R"(f"{(x]}")",
            R"(f"abc)",
            R"(f"{x)",
            "f\"a\nb\"",
            R"(f"\u{d800}")"
        };
        ct::each(invalid, std::identity {}, [&](const char* input) noexcept {
            const auto failure = lex(
                SourceView {.source_id = source.source_id, .text = input, .origin = source.origin}
            );
            ct::expect(!failure.diagnostics.empty()).note("input = ", input);
        });
    });

    ct::test("Lexer: nested interpolation has a bounded scanning depth", [] static noexcept {
        auto source = std::string();
        for (auto index = 0uz; index < 513uz; ++index) {
            source += "f\"{";
        }
        source += '0';
        for (auto index = 0uz; index < 513uz; ++index) {
            source += "}\"";
        }
        const auto result =
            lex(SourceView {
                .source_id = SourceID::from_index(0),
                .text = source,
                .origin = "interpolation-depth.cv"
            });
        if (!ct::expect(!result.diagnostics.empty())) {
            return;
        }
        ct::expect(
            result.diagnostics.front().finding.message == "interpolation nesting limit exceeded"
        );
    });

    ct::test(
        "Lexer: multiline interpolation excludes hole code from text layout",
        [] static noexcept {
            const auto source = SourceView {
                .source_id = SourceID::from_index(0),
                .text = "f\"\"\"\n    Result: {\n0\n    :04}\n      {{done}}\\n\n\"\"\";",
                .origin = "multiline.cv",
            };
            const auto result = lex(source);
            if (!ct::expect(result.diagnostics.empty())) {
                return;
            }
            auto text = std::vector<std::string>();
            for (auto index = 0uz; index < result.value.tokens().size(); ++index) {
                if (result.value.tokens()[index].kind == TokenKind::InterpolationText) {
                    text.push_back(
                        std::get<InterpolationTextValue>(result.value.literal_value(index)).bytes
                    );
                }
            }
            ct::expect(text == std::vector<std::string> {"Result: ", "04", "\n  {done}\n"});
            ct::expect(slice(source.text, result.value.tokens().front().span) == "f\"\"\"\n");
            ct::expect_equal(result.value.tokens().back().kind, TokenKind::Semicolon);
        }
    );

    ct::test("Lexer: multiline interpolation shares text block boundary rules", [] static noexcept {
        struct Case final {
            std::string_view source;
            std::string_view expected;
        };

        const auto cases = std::to_array<Case>({
            {"f\"\"\"\n\"\"\"", ""},
            {"f\"\"\"\n  \n \t\n\"\"\"", "\n"},
            {"f\"\"\"\r\n  A\r\n    B\r\n\"\"\"", "A\n  B"},
            {"f\"\"\"\n  \\n\\u{20}\\0\n\"\"\"", std::string_view("\n \0", 3)},
            {"f\"\"\"\n  \"quoted\"\n  {{literal}}\n\"\"\"", "\"quoted\"\n{literal}"},
            {"f\"\"\"\n    {\n0\n}\n      tail\n\"\"\"", "\n  tail"},
            {"f\"\"\"\n  before {\n0\n} after\n    tail\n\"\"\"", "before  after\n  tail"},
        });
        ct::each(cases, &Case::source, [](const Case& item) static noexcept {
            const auto result =
                lex(SourceView {
                    .source_id = SourceID::from_index(0),
                    .text = item.source,
                    .origin = "multiline.cv",
                });
            if (!(ct::expect(result.diagnostics.empty()).note("item.source = ", item.source))) {
                return;
            }
            auto text = std::string();
            for (auto index = 0uz; index < result.value.tokens().size(); ++index) {
                if (result.value.tokens()[index].kind == TokenKind::InterpolationText) {
                    const auto* value =
                        std::get_if<InterpolationTextValue>(&result.value.literal_value(index));
                    if (!(ct::expect(value != nullptr).note("item.source = ", item.source))) {
                        return;
                    }
                    text += value->bytes;
                }
            }
            ct::expect(text == item.expected).note("item.source = ", item.source);
        });
        const auto invalid = std::to_array<std::string_view>({
            "f\"\"\"inline\"\"\"",
            "f\"\"\"\nA\"\"\"",
            "f\"\"\"\n{0}\"\"\"",
            "f\"\"\"\nA",
            "f\"\"\"\nA\rB\n\"\"\"",
            "f\"\"\"\n\\\n\"\"\"",
            "f\"\"\"\n}\n\"\"\"",
            "f\"\"\"\n\xff\n\"\"\"",
        });
        ct::each(invalid, std::identity {}, [](std::string_view input) static noexcept {
            const auto result =
                lex(SourceView {
                    .source_id = SourceID::from_index(0),
                    .text = input,
                    .origin = "invalid-multiline.cv",
                });
            ct::expect(!(result.diagnostics.empty())).note("input = ", input);
        });
    });
});

} // namespace
