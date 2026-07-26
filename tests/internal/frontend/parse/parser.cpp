module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.parser;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.lex;
import :frontend.lex.token;
import :frontend.parse;
import :source.manager;
import :source.text;
import :test.internal.frontend.parse.fixture;
import std;

TEST_CASE("Parser: empty input owns an empty typed module root") {
    const auto result = parse_valid("");
    const auto& hir_module = root(result);
    CHECK_EQ(hir_module.span.start(), 0u);
    CHECK_EQ(hir_module.span.end(), 0u);
    CHECK(hir_module.imports.empty());
    CHECK(hir_module.items.empty());
}

TEST_CASE("Parser: token source identity crosses the API boundary") {
    auto sources = SourceManager();
    const auto first = *sources.append_virtual("first.cv", "fn first() {}");
    const auto second = *sources.append_virtual("second.cv", "fn second() {}");
    const auto lexical = lex(sources.view(first));
    REQUIRE(lexical.diagnostics.empty());
    CHECK_EQ(lexical.value.source_id(), first);
    CHECK_NE(lexical.value.source_id(), second);

    const auto parsed = parse(sources, lexical.value);
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->view().source_id(), first);
    CHECK_EQ(slice(sources.view(first).text, function(*parsed).name_span), "first");
}

TEST_CASE("Parser: syntax diagnostics use one structured primary label") {
    const auto result = parse_source("fn main(1) {}");
    REQUIRE(!result.has_value());
    REQUIRE_EQ(result.error().size(), 1u);
    CHECK_EQ(result.error()[0].finding.message, "expected parameter name");
    REQUIRE(result.error()[0].attachment.primary.has_value());
    CHECK_EQ(result.error()[0].attachment.primary->span.span.start(), 8u);
    CHECK_EQ(result.error()[0].attachment.primary->span.span.end(), 9u);
}

TEST_CASE("Parser: delimiter preflight reports mismatched and unclosed delimiters") {
    const auto mismatched = parse_source("fn main() { let value = [1); }");
    REQUIRE(!mismatched.has_value());
    REQUIRE_EQ(mismatched.error().size(), 1u);
    CHECK_EQ(
        mismatched.error().front().finding.message,
        "mismatched closing delimiter ')'; expected ']'"
    );
    REQUIRE(mismatched.error().front().attachment.primary.has_value());
    CHECK_EQ(mismatched.error().front().attachment.primary->span.span.start(), 26u);
    CHECK_EQ(mismatched.error().front().attachment.primary->span.span.end(), 27u);

    const auto unclosed = parse_source("fn main() {");
    REQUIRE(!unclosed.has_value());
    REQUIRE_EQ(unclosed.error().size(), 1u);
    CHECK_EQ(unclosed.error().front().finding.message, "unclosed delimiter '{'; expected '}'");
    REQUIRE(unclosed.error().front().attachment.primary.has_value());
    CHECK_EQ(unclosed.error().front().attachment.primary->span.span.start(), 10u);
    CHECK_EQ(unclosed.error().front().attachment.primary->span.span.end(), 11u);
}

TEST_CASE("Parser: committed item recovery reports independent declarations") {
    const auto result = parse_source(
        "fn first(1) {}\n"
        "fn second(2) {}\n"
    );
    REQUIRE(!result.has_value());
    REQUIRE_EQ(result.error().size(), 2u);
    CHECK_EQ(result.error()[0].finding.message, "expected parameter name");
    CHECK_EQ(result.error()[1].finding.message, "expected parameter name");
}

TEST_CASE("Parser: visibility and constant starts participate in item recovery") {
    const auto result = parse_source(
        "const first =;\n"
        "private const second =;\n"
    );
    REQUIRE(!result.has_value());
    REQUIRE_EQ(result.error().size(), 2u);
    CHECK_EQ(result.error()[0].finding.message, "expected expression");
    CHECK_EQ(result.error()[1].finding.message, "expected expression");
}

TEST_CASE("Parser: later speculation never replaces a committed diagnostic") {
    const auto result = parse_source(
        "fn bad() -> i32 { return 1 + }\n"
        "fn good() -> i32 { return 2; }\n"
        "test \"probe\" { check(good() == 2); }\n"
    );
    REQUIRE(!result.has_value());
    REQUIRE_EQ(result.error().size(), 1u);
    CHECK_EQ(result.error()[0].finding.message, "expected expression");
    REQUIRE(result.error()[0].attachment.primary.has_value());
    CHECK_LT(result.error()[0].attachment.primary->span.span.start(), 33u);
}

TEST_CASE("Parser: a failed C-style for step restores construction boundaries") {
    static constexpr auto source = std::string_view(
        "fn broken() { for ;; value = { } }\n"
        "fn recovered() { let value = Model { field: 1 }; }\n"
    );
    static constexpr auto invalid_step = source.find("value =");
    static constexpr auto recovered_declaration = source.find("fn recovered");
    const auto result = parse_source(source);
    REQUIRE(!result.has_value());
    REQUIRE_EQ(result.error().size(), 1u);
    CHECK_EQ(result.error().front().finding.code, DiagnosticCode::Syntax);
    REQUIRE(result.error().front().attachment.primary.has_value());
    const auto diagnostic_start = result.error().front().attachment.primary->span.span.start();
    CHECK_GE(diagnostic_start, invalid_step);
    CHECK_LT(diagnostic_start, recovered_declaration);
}

TEST_CASE("Parser: excessive syntax nesting returns a deterministic diagnostic") {
    auto source = std::string("fn deep() { let value = ");
    source.append(600, '(');
    source += '1';
    source.append(600, ')');
    source += "; }";

    const auto result = parse_source(source);
    REQUIRE(!result.has_value());
    CHECK(std::ranges::any_of(result.error(), [](const Diagnostic& diagnostic) static noexcept {
        return diagnostic.finding.code == "CV-PARSE-NESTING-TOO-DEEP";
    }));
}

TEST_CASE("Parser: moving SyntaxTree preserves its module and owned IDs") {
    static constexpr auto text = std::string_view(
        "import .helper using helper;\n"
        "fn answer() -> i32 { return 42; }"
    );
    auto parsed = parse_source(text);
    REQUIRE(parsed.has_value());
    const auto import_id = root(*parsed).imports.front();
    const auto item_id = root(*parsed).items.front();

    auto moved = std::move(parsed);
    REQUIRE(moved.has_value());
    const auto ast = moved->view();
    CHECK_EQ(ast.ast_module().items.size(), 1u);
    CHECK_EQ(
        slice(
            text,
            std::get<ASTParentRelativeModuleReference>(
                ast.import_declaration(import_id).module_reference.value
            )
                .components.front()
        ),
        "helper"
    );
    CHECK_EQ(slice(text, get<ASTFunctionDecl>(ast.item(item_id)).name_span), "answer");
}

TEST_CASE("Parser: AST storage builder creates a complete ID graph") {
    const auto parsed =
        parse_valid("fn answer(value: [i32; 4]) -> i32 { let values = [1, 2]; return value; }");
    const auto ast = parsed.view();
    CHECK_EQ(ast.ast_module().items.size(), 1u);
    CHECK_EQ(ast.items().size(), 1u);
    CHECK(!ast.types().empty());
    CHECK(!ast.expressions().empty());
    CHECK(!ast.statements().empty());
}

TEST_CASE("Parser: failed speculation rewinds every typed table") {
    const auto valid = parse_valid("fn f() { let value = Vec { x: 1, y: 2 }; }");
    check_invalid(
        "fn f() { let value = Vec { x: 1, y }; }",
        "expected ':' after initializer field name"
    );
    check_invalid("fn f() { for * ; ready; tick() {} }");
    check_invalid("fn f() { for value; ready; * {} }");
}
