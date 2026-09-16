module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.graver.format;

import :diagnostics.diagnostic;
import :diagnostics.report;
import :graver.format;
import :source.manager;
import :support.path;
import std;

namespace {

auto check_format(std::string_view input, std::string_view expected) noexcept -> void {
    auto sources = SourceManager();
    const auto id = sources.append_virtual("input.cv", std::string(input));
    REQUIRE(id.has_value());
    const auto result = graver::format(sources, *id);
    if (!result) {
        INFO(render_diagnostics(result.error(), sources));
        REQUIRE(result.has_value());
    }
    CHECK(*result == expected);
    const auto formatted_id = sources.append_virtual("formatted.cv", *result);
    REQUIRE(formatted_id.has_value());
    const auto repeated = graver::format(sources, *formatted_id);
    REQUIRE(repeated.has_value());
    CHECK(*repeated == *result);
    CHECK(sources.view(*id).text == input);
}

} // namespace

TEST_CASE("Graver format: examples have exact and stable output") {
    auto folders = std::vector<std::filesystem::path>();
    auto error = std::error_code();
    auto iterator = std::filesystem::directory_iterator("tools/graver/tests/format", error);
    REQUIRE_FALSE(error);
    const auto end = std::filesystem::directory_iterator();
    while (iterator != end) {
        if (iterator->is_directory(error)) {
            folders.push_back(iterator->path());
        }
        REQUIRE_FALSE(error);
        iterator.increment(error);
        REQUIRE_FALSE(error);
    }
    std::ranges::sort(folders);
    REQUIRE_FALSE(folders.empty());
    for (const auto& folder : folders) {
        const auto name = path_to_generic_utf8(folder);
        INFO(name);
        auto sources = SourceManager();
        const auto input_id = sources.append_file(path_to_generic_utf8(folder / "input.cv"));
        REQUIRE(input_id.has_value());
        const auto expected_id = sources.append_file(path_to_generic_utf8(folder / "expected.cv"));
        REQUIRE(expected_id.has_value());
        check_format(sources.view(*input_id).text, sources.view(*expected_id).text);
    }
}

TEST_CASE(
    "Graver format: comments retain their token gaps including punctuation and closing braces"
) {
    check_format(
        "// header\r\nfn f(){let x=1;// tail\r\n// next\r\ncall(x // arg\r\n,2);\r\n// closing\r\n}// eof",
        "// header\nfn f() {\n    let x = 1; // tail\n    // next\n    call(\n        x // arg\n        ,\n        2\n    );\n    // closing\n} // eof\n"
    );
}

TEST_CASE("Graver format: empty files comments blank lines and CRLF have stable output") {
    check_format("", "");
    check_format(" \t\r\n", "\n");
    check_format(" \t// only  \r\n\r\n", "// only  \n\n");
    check_format(
        "fn f(){}\r\n\r\n\r\nfn g(){\r\nlet x=1;\r\n\r\n\r\nlet y=2;\r\n}",
        "fn f() {}\n\n\nfn g() {\n    let x = 1;\n\n\n    let y = 2;\n}\n"
    );
}

TEST_CASE("Graver format: literal text interpolation and fenced C++ remain exact") {
    check_format(
        "fn f(){let s=\"\\u{41}// literal\";let n=0xFFu32;let t=f\"hi { x // hole\r\n :0{w}x}\";}",
        "fn f() {\n    let s = \"\\u{41}// literal\";\n    let n = 0xFFu32;\n    let t = f\"hi {x // hole\n        :0{w}x}\";\n}\n"
    );
    check_format(
        "#[cpp] ---\r\n\t// C++  \r\n---\r\nfn f(){}",
        "#[cpp] ---\r\n\t// C++  \r\n---\n\nfn f() {}\n"
    );
}

TEST_CASE("Graver format: invalid syntax fails without changing input") {
    const auto cases = std::to_array<std::string_view>({
        "fn f(",
        "@",
    });
    for (const auto input : cases) {
        CAPTURE(input);
        auto sources = SourceManager();
        const auto id = sources.append_virtual("rejected.cv", std::string(input));
        REQUIRE(id.has_value());
        const auto result = graver::format(sources, *id);
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(result.error().empty());
        CHECK(sources.view(*id).text == input);
    }
}

TEST_CASE("Graver format: import lists include closing punctuation in the line width") {
    for (const auto prefix :
         {std::string_view("import <vector> using std::{"),
          std::string_view("import std::utf.text using {")}) {
        const auto name = std::string(100uz - prefix.size() - 2uz, 'x');
        const auto fitting = std::string(prefix) + name + "};";
        check_format(fitting, fitting + "\n");
        check_format(
            std::string(prefix) + name + "x};",
            std::string(prefix) + "\n    " + name + "x\n};\n"
        );
    }
}

TEST_CASE("Graver format: unbounded range patterns separate their guards") {
    check_format(
        "fn f(x:i32){match x{0..if ready=>{},_=>{},}}",
        "fn f(x: i32) {\n    match x {\n        0.. if ready => {},\n        _ => {},\n    }\n}\n"
    );
}
