module carven:test.graver.format;

import :diagnostics.diagnostic;
import :diagnostics.report;
import :graver.format;
import :graver.source;
import :source.manager;
import :support.path;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

auto check_format(std::string_view input, std::string_view expected) noexcept -> void {
    auto sources = SourceManager();
    const auto id = sources.append_virtual("input.cv", std::string(input));
    if (!ct::expect(id.has_value())) {
        return;
    }
    const auto result = graver::format(sources, *id);
    if (!ct::expect(result.has_value()).note([&] noexcept {
            return render_diagnostics(result.error(), sources);
        })) {
        return;
    }
    ct::expect_equal(*result, expected);
    const auto formatted_id = sources.append_virtual("formatted.cv", *result);
    if (!ct::expect(formatted_id.has_value())) {
        return;
    }
    const auto repeated = graver::format(sources, *formatted_id);
    if (!ct::expect(repeated.has_value()).note([&] noexcept {
            return render_diagnostics(repeated.error(), sources);
        })) {
        return;
    }
    ct::expect_equal(*repeated, *result);
    ct::expect_equal(sources.view(*id).text, input);
    const auto lexical = graver::Source::scan(sources.view(*id));
    if (!ct::expect(lexical.has_value())) {
        return;
    }
    auto perturbed = std::string();
    const auto tokens = lexical->token_buffer().tokens();
    for (auto index = 0uz; index <= tokens.size(); ++index) {
        for (const auto trivia : lexical->trivia_before(index)) {
            perturbed += trivia.kind == graver::TriviaKind::HorizontalWhitespace
                ? " \t  "
                : lexical->spelling(trivia.span);
        }
        if (index < tokens.size()) {
            perturbed += lexical->spelling(tokens[index].span);
        }
    }
    if (perturbed == input) {
        return;
    }
    const auto perturbed_id = sources.append_virtual("perturbed.cv", std::move(perturbed));
    if (!ct::expect(perturbed_id.has_value())) {
        return;
    }
    const auto normalized = graver::format(sources, *perturbed_id);
    if (!ct::expect(normalized.has_value()).note([&] noexcept {
            return render_diagnostics(normalized.error(), sources);
        })) {
        return;
    }
    ct::expect_equal(*normalized, expected);
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Graver format: examples have exact and stable output", [] static noexcept {
        auto folders = std::vector<std::filesystem::path>();
        auto error = std::error_code();
        auto iterator = std::filesystem::directory_iterator("tools/graver/tests/format", error);
        if (!ct::expect(!(error))) {
            return;
        }
        const auto end = std::filesystem::directory_iterator();
        while (iterator != end) {
            if (iterator->is_directory(error)) {
                folders.push_back(iterator->path());
            }
            if (!ct::expect(!(error))) {
                return;
            }
            iterator.increment(error);
            if (!ct::expect(!(error))) {
                return;
            }
        }
        std::ranges::sort(folders);
        ct::each(
            folders,
            [](const auto& folder) static noexcept { return path_to_generic_utf8(folder); },
            [&](const auto& folder) noexcept {
                auto sources = SourceManager();
                const auto input_id =
                    sources.append_file(path_to_generic_utf8(folder / "input.cv"));
                if (!ct::expect(input_id.has_value())) {
                    return;
                }
                const auto expected_id =
                    sources.append_file(path_to_generic_utf8(folder / "expected.cv"));
                if (!ct::expect(expected_id.has_value())) {
                    return;
                }
                check_format(sources.view(*input_id).text, sources.view(*expected_id).text);
            }
        );
    });

    ct::test(
        "Graver format: comments retain their token gaps including punctuation and closing braces",
        [] static noexcept {
            check_format(
                "// header\r\nfn f(){let x=1;// tail\r\n// next\r\ncall(x // arg\r\n,2);\r\n// closing\r\n}// eof",
                "// header\nfn f() {\n    let x = 1; // tail\n    // next\n    call(\n        x // arg\n        ,\n        2\n    );\n    // closing\n} // eof\n"
            );
        }
    );

    ct::test(
        "Graver format: empty files comments blank lines and CRLF have stable output",
        [] static noexcept {
            check_format("", "");
            check_format("\n\n\nfn f(){}\n\n\n", "\nfn f() {}\n\n");
            check_format(" \t\r\n", "\n");
            check_format(" \t// only  \r\n\r\n", "// only  \n\n");
            check_format(
                "fn f(){}\r\n\r\n\r\nfn g(){\r\nlet x=1;\r\n\r\n\r\nlet y=2;\r\n}",
                "fn f() {}\n\nfn g() {\n    let x = 1;\n\n    let y = 2;\n}\n"
            );
        }
    );

    ct::test(
        "Graver format: top-level spacing combines category boundaries and authored groups",
        [] static noexcept {
            check_format(
                "const \"first\"{}\nconst \"second\"{}\nconst test{}\nconst test{}\n"
                "test{}\ntest{}\nstruct First{}\nexport struct Second{}\nfn one(){}\nfn two(){}",
                "const \"first\" {}\nconst \"second\" {}\n\nconst test {}\nconst test {}\n\n"
                "test {}\ntest {}\n\nstruct First {}\nexport struct Second {}\n\nfn one() {}\nfn two() {}\n"
            );
            check_format(
                "test{}\n\n\ntest{}\nprintln(1);\n\n\nprintln(2);",
                "test {}\n\ntest {}\n\nprintln(1);\n\nprintln(2);\n"
            );
            check_format(
                "test{} // previous\n\n\n// next\n\n\ntest{}",
                "test {} // previous\n\n// next\n\ntest {}\n"
            );
        }
    );

    ct::test("Graver format: literal text interpolation and fenced C++ remain exact", [] static noexcept {
        check_format(
            "fn f(){let s=\"\\u{41}// literal\";let n=0xFFu32;let t=f\"hi { x // hole\r\n :0{w}x}\";}",
            "fn f() {\n    let s = \"\\u{41}// literal\";\n    let n = 0xFFu32;\n    let t = f\"hi {x // hole\n        :0{w}x}\";\n}\n"
        );
        check_format(
            "#[cpp] ---\r\n\t// C++  \r\n---\r\nfn f(){}",
            "#[cpp] ---\r\n\t// C++  \r\n---\n\nfn f() {}\n"
        );
    });

    ct::test("Graver format: invalid syntax fails without changing input", [] static noexcept {
        const auto cases = std::to_array<std::pair<std::string_view, std::string_view>>({
            {"Incomplete parameter list", "fn f("},
            {"Invalid character", "@"},
        });
        ct::each(
            cases,
            [](const auto& entry) static noexcept { return entry.first; },
            [&](const auto& entry) noexcept {
                const auto input = entry.second;
                auto sources = SourceManager();
                const auto id = sources.append_virtual("rejected.cv", std::string(input));
                if (!ct::expect(id.has_value())) {
                    return;
                }
                const auto result = graver::format(sources, *id);
                if (!ct::expect(!result.has_value())) {
                    return;
                }
                ct::expect(!result.error().empty());
                ct::expect_equal(sources.view(*id).text, input);
            }
        );
    });

    ct::test(
        "Graver format: import lists include closing punctuation in the line width",
        [] static noexcept {
            const auto prefixes = std::to_array<std::string_view>({
                "import <vector> using std::{",
                "import std::utf.text using {",
            });
            ct::each(
                prefixes,
                [](std::string_view prefix) static noexcept { return prefix; },
                [&](std::string_view prefix) noexcept {
                    const auto name = std::string(100uz - prefix.size() - 4uz, 'x');
                    const auto fitting = std::string(prefix) + " " + name + " };";
                    check_format(fitting, fitting + "\n");
                    check_format(std::string(prefix) + name + ",};", fitting + "\n");
                    check_format(
                        std::string(prefix) + name + "x};",
                        std::string(prefix) + "\n    " + name + "x,\n};\n"
                    );
                }
            );
        }
    );

    ct::test("Graver format: unbounded range patterns separate their guards", [] static noexcept {
        check_format(
            "fn f(x:i32){match x{0..if ready=>{},_=>{},}}",
            "fn f(x: i32) {\n    match x {\n        0.. if ready => {},\n        _ => {},\n    }\n}\n"
        );
    });

    ct::test(
        "Graver format: import trailing commas follow layout and preserve comments",
        [] static noexcept {
            const auto prefixes = std::to_array<std::string_view>({
                "import <print> using std::{",
                "import \"provider.hpp\" using {",
                "import math using {",
            });
            ct::each(
                prefixes,
                [](std::string_view prefix) static noexcept { return prefix; },
                [&](std::string_view prefix) noexcept {
                    check_format(
                        std::string(prefix) + "first, second,};",
                        std::string(prefix) + " first, second };\n"
                    );
                    check_format(
                        std::string(prefix) + "\nfirst,\nsecond,\n};",
                        std::string(prefix) + " first, second };\n"
                    );
                    check_format(
                        std::string(prefix) + "first // last name\n};",
                        std::string(prefix) + "\n    first, // last name\n};\n"
                    );
                    check_format(
                        std::string(prefix) + "first, // last comma\n};",
                        std::string(prefix) + "\n    first, // last comma\n};\n"
                    );
                    check_format(
                        std::string(prefix) + "first // before comma\n, // after comma\n};",
                        std::string(prefix)
                            + "\n    first // before comma\n    , // after comma\n};\n"
                    );
                    check_format(
                        std::string(prefix) + "first\n\n};",
                        std::string(prefix) + "\n    first,\n\n};\n"
                    );
                }
            );
            check_format(
                "import math using {first,}; fn f(){call(1,);}",
                "import math using { first };\n\nfn f() {\n    call(1,);\n}\n"
            );
        }
    );

    ct::test("Graver format: raw and multiline text retain their authored layout", [] static noexcept {
        check_format(
            "fn f(){let raw=r#\"{value} \\n\"#;let block=r\"\"\"\n  A\n    B\n\"\"\";"
            "let message=f\"\"\"\n  {value}\n    tail\n\"\"\";}",
            "fn f() {\n    let raw = r#\"{value} \\n\"#;\n    let block = r\"\"\"\n  A\n    B\n\"\"\";\n"
            "    let message = f\"\"\"\n  {value}\n    tail\n\"\"\";\n}\n"
        );
    });

    ct::test(
        "Graver format: multiline interpolation text and hole layout are independent",
        [] static noexcept {
            check_format(
                "fn f(){let s=f\"\"\"\n  first\n  { value+1 }\n    tail\n\"\"\";}",
                "fn f() {\n    let s = f\"\"\"\n  first\n  {value + 1}\n    tail\n\"\"\";\n}\n"
            );
            check_format(
                "fn f(){let s=f\"\"\"\r\n{\r\n value // note\r\n +1\r\n} tail\r\n\"\"\";}",
                "fn f() {\n    let s = f\"\"\"\r\n{value // note\n            + 1} tail\r\n\"\"\";\n}\n"
            );
            check_format(
                "fn f(){let s=f\"\"\"\n  {f\"\"\"\n    { value+1 }\n\"\"\"}\n  tail\n\"\"\";}",
                "fn f() {\n    let s = f\"\"\"\n  {f\"\"\"\n    {value + 1}\n\"\"\"}\n  tail\n\"\"\";\n}\n"
            );
        }
    );
});

} // namespace
