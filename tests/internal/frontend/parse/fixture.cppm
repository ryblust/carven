module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.fixture;

import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.lex;
import :frontend.lex.token;
import :frontend.parse;
import :source.manager;
import :source.text;
import std;

auto parse_source(std::string_view text) noexcept -> std::expected<SyntaxTree, Diagnostics> {
    auto sources = SourceManager();
    const auto source = *sources.append_virtual("parser-test.cv", std::string(text));
    const auto lexical = lex(sources.view(source));
    CAPTURE(text);
    REQUIRE(lexical.diagnostics.empty());
    return parse(sources, lexical.value);
}

auto parse_valid(std::string_view text) noexcept -> SyntaxTree {
    auto result = parse_source(text);
    CAPTURE(text);
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto check_invalid(std::string_view text) noexcept -> void {
    const auto result = parse_source(text);
    CAPTURE(text);
    REQUIRE(!result.has_value());
    REQUIRE_EQ(result.error().size(), 1u);
}

auto check_invalid(std::string_view text, std::string_view message) noexcept -> void {
    const auto result = parse_source(text);
    CAPTURE(text);
    REQUIRE(!result.has_value());
    REQUIRE_EQ(result.error().size(), 1u);
    CHECK_EQ(result.error()[0].finding.message, message);
}

auto check_rejected(std::string_view text) noexcept -> void {
    const auto result = parse_source(text);
    CAPTURE(text);
    REQUIRE(!result.has_value());
    REQUIRE(!result.error().empty());
}

template<typename Alternative, typename Family>
    requires requires (const Family& family) { family.value; }
auto is(const Family& family) noexcept -> bool {
    return std::holds_alternative<Alternative>(family.value);
}

template<typename Alternative, typename... Values>
auto is(const std::variant<Values...>& value) noexcept -> bool {
    return std::holds_alternative<Alternative>(value);
}

template<typename Alternative, typename Family>
auto get(const Family& family) noexcept -> const Alternative& {
    return std::get<Alternative>(family.value);
}

auto root(const SyntaxTree& tree) noexcept -> const ASTModule& {
    return tree.view().ast_module();
}

auto item(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTItem& {
    const auto ast = tree.view();
    return ast.item(ast.ast_module().items[index]);
}

auto function(const SyntaxTree& tree, std::size_t index = 0) noexcept -> const ASTFunctionDecl& {
    return get<ASTFunctionDecl>(item(tree, index));
}

auto function_body(const SyntaxTree& tree, std::size_t index = 0) noexcept -> const ASTBlock& {
    const auto ast = tree.view();
    const auto& implementation = get<ASTFunctionBody>(function(tree, index).implementation);
    return ast.block(implementation.body);
}
