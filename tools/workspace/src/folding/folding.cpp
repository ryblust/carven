module carven:workspace.folding.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.document;
import :frontend.lex.token;
import :source.manager;
import :source.text;
import :workspace.folding;
import std;

namespace {

class FoldCollector final {
public:
    explicit FoldCollector(const ParsedDocument& document) noexcept;
    auto collect() noexcept -> WorkspaceFoldList;

private:
    auto add(Span range, WorkspaceFoldKind kind) noexcept -> void;
    auto delimited(Span range, TokenKind open, TokenKind close, std::uint32_t from) noexcept
        -> void;
    auto comments() noexcept -> void;
    auto imports(ASTView syntax) noexcept -> void;
    auto line(std::uint32_t offset) const noexcept -> std::uint32_t;

    const ParsedDocument& document;
    SourceView source;
    WorkspaceFoldList result;
    std::span<const Token> tokens;
};

FoldCollector::FoldCollector(const ParsedDocument& document) noexcept
    : document(document),
      source(document.source()) {}

auto FoldCollector::line(std::uint32_t offset) const noexcept -> std::uint32_t {
    return document.sources().location({source.source_id, Span::at(offset)}).line;
}

auto FoldCollector::add(Span range, WorkspaceFoldKind kind) noexcept -> void {
    if (kind != WorkspaceFoldKind::Code) {
        const auto end = source.text.find_first_of("\r\n", range.start());
        if (end != std::string_view::npos && end < range.end()) {
            range = Span::from_bounds(static_cast<std::uint32_t>(end), range.end());
        }
    }
    if (range.start() < range.end() && line(range.start()) < line(range.end())) {
        result.push_back({.range = range, .kind = kind});
    }
}

auto FoldCollector::delimited(
    Span range,
    TokenKind open,
    TokenKind close,
    std::uint32_t from
) noexcept -> void {
    const auto begin =
        std::ranges::lower_bound(tokens, from, {}, [](const Token& token) static noexcept {
            return token.span.start();
        });
    const auto opening = std::find_if(begin, tokens.end(), [&](const Token& token) noexcept {
        return token.span.start() >= range.end() || token.kind == open;
    });
    if (opening == tokens.end() || opening->span.start() >= range.end()) {
        return;
    }
    const auto after =
        std::ranges::lower_bound(tokens, range.end(), {}, [](const Token& token) static noexcept {
            return token.span.start();
        });
    auto end = range.end();
    if (after != tokens.begin() && (after - 1)->kind == close) {
        end = (after - 1)->span.start();
    }
    if (opening->span.end() <= end) {
        add(Span::from_bounds(opening->span.end(), end), WorkspaceFoldKind::Code);
    }
}

auto FoldCollector::comments() noexcept -> void {
    auto group = std::optional<Span>();
    const auto flush = [&]() noexcept {
        if (group) {
            add(*group, WorkspaceFoldKind::Comment);
            group.reset();
        }
    };
    auto previous = 0u;
    const auto gap = [&](std::uint32_t end) noexcept {
        auto cursor = previous;
        while (cursor < end) {
            const auto start = source.text.find("//", cursor);
            if (start == std::string_view::npos || start >= end) {
                break;
            }
            auto finish = source.text.find_first_of("\r\n", start);
            if (finish == std::string_view::npos || finish > end) {
                finish = end;
            }
            const auto at = static_cast<std::uint32_t>(start);
            const auto line_start =
                document.sources().line_span(source.source_id, line(at)).start();
            const auto prefix = source.text.substr(line_start, start - line_start);
            if (std::ranges::all_of(prefix, [](char byte) static noexcept {
                    return byte == ' ' || byte == '\t';
                })) {
                if (group && line(group->end()) + 1 == line(at)) {
                    group = Span::from_bounds(group->start(), static_cast<std::uint32_t>(finish));
                } else {
                    flush();
                    group = Span::from_bounds(at, static_cast<std::uint32_t>(finish));
                }
            } else {
                flush();
            }
            cursor = static_cast<std::uint32_t>(finish);
        }
    };
    for (const auto& token : tokens) {
        gap(token.span.start());
        previous = token.span.end();
    }
    gap(static_cast<std::uint32_t>(source.text.size()));
    flush();
}

auto FoldCollector::imports(ASTView syntax) noexcept -> void {
    auto spans = std::vector<Span>();
    for (const auto id : syntax.ast_module().module_imports) {
        spans.push_back(syntax.module_import(id).span);
    }
    for (const auto& header : syntax.ast_module().cpp_header_imports) {
        spans.push_back(header.span);
    }
    std::ranges::sort(spans);
    auto group = std::optional<Span>();
    for (const auto span : spans) {
        if (group) {
            const auto between = source.text.substr(group->end(), span.start() - group->end());
            if (line(group->end()) + 1 >= line(span.start())
                && std::ranges::all_of(between, [](char byte) static noexcept {
                       return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
                   })) {
                group = Span::from_bounds(group->start(), span.end());
                continue;
            }
            add(*group, WorkspaceFoldKind::Imports);
        }
        group = span;
    }
    if (group) {
        add(*group, WorkspaceFoldKind::Imports);
    }
}

auto FoldCollector::collect() noexcept -> WorkspaceFoldList {
    tokens = document.tokens();
    comments();
    if (const auto syntax = document.recovered_syntax()) {
        imports(*syntax);
        for (const auto& block : syntax->blocks()) {
            delimited(block.span, TokenKind::LeftBrace, TokenKind::RightBrace, block.span.start());
        }
        for (const auto& block : syntax->branch_blocks()) {
            if (!block.arrow_span) {
                delimited(
                    block.span,
                    TokenKind::LeftBrace,
                    TokenKind::RightBrace,
                    block.span.start()
                );
            }
        }
        for (const auto& item : syntax->items()) {
            item.value.visit([&](const auto& declaration) noexcept {
                using T = std::decay_t<decltype(declaration)>;
                if constexpr (std::same_as<T, ASTRecordDecl> || std::same_as<T, ASTEnumDecl>) {
                    delimited(
                        item.span,
                        TokenKind::LeftBrace,
                        TokenKind::RightBrace,
                        declaration.name_span.end()
                    );
                }
            });
        }
        for (const auto& expression : syntax->expressions()) {
            expression.value.visit([&](const auto& value) noexcept {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<T, ASTArrayExpr>
                              || std::same_as<T, ASTArrayRepeatExpr>) {
                    delimited(
                        expression.span,
                        TokenKind::LeftBracket,
                        TokenKind::RightBracket,
                        expression.span.start()
                    );
                } else if constexpr (std::same_as<T, ASTTupleExpr>
                                     || std::same_as<T, ASTGroupExpr>) {
                    delimited(
                        expression.span,
                        TokenKind::LeftParen,
                        TokenKind::RightParen,
                        expression.span.start()
                    );
                } else if constexpr (std::same_as<T, ASTCallExpr>) {
                    delimited(
                        expression.span,
                        TokenKind::LeftParen,
                        TokenKind::RightParen,
                        syntax->expression(value.callee).span.end()
                    );
                } else if constexpr (std::same_as<T, ASTConstructionExpr>) {
                    value.initializer.value.visit([&](const auto& list) noexcept {
                        if constexpr (requires { list.span; }) {
                            delimited(
                                list.span,
                                TokenKind::LeftBrace,
                                TokenKind::RightBrace,
                                list.span.start()
                            );
                        }
                    });
                }
            });
        }
        const auto control = [&](const auto& value) noexcept {
            if (const auto* match = std::get_if<ASTMatchForm>(&value)) {
                delimited(
                    match->span,
                    TokenKind::LeftBrace,
                    TokenKind::RightBrace,
                    syntax->expression(match->subject).span.end()
                );
            }
        };
        for (const auto& statement : syntax->statements()) {
            control(statement.value);
        }
        for (const auto& expression : syntax->expressions()) {
            control(expression.value);
        }
    }
    std::ranges::sort(result, [](const WorkspaceFold& a, const WorkspaceFold& b) static noexcept {
        return std::tuple(
                   a.range.start(),
                   std::numeric_limits<std::uint32_t>::max() - a.range.end(),
                   a.kind
               )
            < std::tuple(
                   b.range.start(),
                   std::numeric_limits<std::uint32_t>::max() - b.range.end(),
                   b.kind
            );
    });
    result.erase(
        std::unique(
            result.begin(),
            result.end(),
            [](const WorkspaceFold& a, const WorkspaceFold& b) static noexcept {
                return a.range == b.range;
            }
        ),
        result.end()
    );
    return std::move(result);
}

} // namespace

auto collect_workspace_folds(const ParsedDocument& document) noexcept -> WorkspaceFoldList {
    return FoldCollector(document).collect();
}
