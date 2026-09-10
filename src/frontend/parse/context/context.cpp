module carven:frontend.parse.context.impl;

import :diagnostics.builder;
import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.parse.builder;
import :frontend.parse.context;
import :source.text;
import std;

Parser::Parser(SourceView source_view, const TokenBuffer& token_buffer) noexcept
    : source(source_view.text),
      source_id(source_view.source_id),
      token_buffer(std::addressof(token_buffer)),
      tokens(token_buffer.tokens()),
      builder(source_view) {}

Parser::DepthGuard::DepthGuard(std::uint32_t* depth) noexcept
    : depth(depth) {}

Parser::DepthGuard::~DepthGuard() noexcept {
    if (depth != nullptr) {
        --*depth;
    }
}

Parser::DepthGuard::operator bool() const noexcept {
    return depth != nullptr;
}

auto Parser::enter_depth(std::uint32_t& depth) noexcept -> DepthGuard {
    ++depth;
    return DepthGuard(&depth);
}

Parser::TestStatementContextGuard::TestStatementContextGuard(bool& context, bool enabled) noexcept
    : context(&context),
      prior(context) {
    context = enabled;
}

Parser::TestStatementContextGuard::~TestStatementContextGuard() noexcept {
    *context = prior;
}

auto Parser::enter_test_statement_context(bool enabled) noexcept -> TestStatementContextGuard {
    return TestStatementContextGuard(test_statements_enabled, enabled);
}

auto Parser::run() noexcept -> std::expected<SyntaxTree, Diagnostics> {
    if (!preflight_delimiter_nesting()) {
        return std::unexpected(std::move(diagnostics));
    }

    auto module_imports = std::vector<ASTModuleImportID> {};
    auto cpp_header_imports = std::vector<ASTCppHeaderImport> {};
    auto cpp_source_fragments = std::vector<ASTCppSourceFragment> {};
    auto items = std::vector<ASTItemID> {};

    while (!failed && check(TokenKind::Import) && !check_next(TokenKind::LeftParen)) {
        if (check_next(TokenKind::CppAngleHeaderName)
            || check_next(TokenKind::CppQuoteHeaderName)) {
            cpp_header_imports.push_back(parse_cpp_header_import());
        } else {
            module_imports.push_back(parse_module_import());
        }
    }

    while (!failed && !at_end()) {
        if (check(TokenKind::CppSourceFragment)) {
            cpp_source_fragments.push_back(make_cpp_source_fragment(consume().span));
            continue;
        }
        furthest_speculative_failure.reset();
        const auto diagnostic_count = diagnostics.size();
        const auto checkpoint = builder.checkpoint();
        const auto item = parse_top_level_item();
        if (item.has_value()) {
            items.push_back(*item);
            continue;
        }
        if (furthest_speculative_failure.has_value()
            && (diagnostics.size() == diagnostic_count
                || !diagnostics[diagnostic_count].attachment.primary.has_value()
                || furthest_speculative_failure->span.start()
                    >= diagnostics[diagnostic_count].attachment.primary->span.span.start())) {
            diagnostics.resize(diagnostic_count);
            const auto& failure = *furthest_speculative_failure;
            diagnostics.push_back(DiagnosticBuilder(failure.code, failure.message)
                                      .primary(locate(source_id, failure.span))
                                      .build());
        }
        furthest_speculative_failure.reset();
        builder.rewind(checkpoint);
        failed = false;
        synchronize_top_level_item();
    }

    if (failed || !diagnostics.empty()) {
        if (diagnostics.empty() && furthest_speculative_failure.has_value()) {
            const auto& failure = *furthest_speculative_failure;
            diagnostics.push_back(DiagnosticBuilder(failure.code, failure.message)
                                      .primary(locate(source_id, failure.span))
                                      .build());
        }
        return std::unexpected(std::move(diagnostics));
    }

    auto ast_module = ASTModule {
        .span = Span::from_bounds(0, static_cast<std::uint32_t>(source.size())),
        .module_imports = std::move(module_imports),
        .cpp_header_imports = std::move(cpp_header_imports),
        .cpp_source_fragments = std::move(cpp_source_fragments),
        .items = std::move(items),
    };
    return std::move(builder).finish(std::move(ast_module));
}

auto Parser::synchronize_top_level_item() noexcept -> void {
    auto brace_depth = 0uz;
    for (auto index = 0uz; index < cursor && index < tokens.size(); ++index) {
        if (tokens[index].kind == TokenKind::LeftBrace) {
            ++brace_depth;
        }
        if (tokens[index].kind == TokenKind::RightBrace && brace_depth != 0) {
            --brace_depth;
        }
    }
    const auto starts_item = [](TokenKind kind) static noexcept -> bool {
        return kind == TokenKind::Private
            || kind == TokenKind::Export
            || kind == TokenKind::Enum
            || kind == TokenKind::Struct
            || kind == TokenKind::Fn
            || kind == TokenKind::Const
            || kind == TokenKind::Test
            || kind == TokenKind::Import
            || kind == TokenKind::CppSourceFragment;
    };
    while (!at_end()) {
        if (brace_depth == 0 && starts_item(current().kind)) {
            return;
        }
        const auto token = consume();
        if (token.kind == TokenKind::LeftBrace) {
            ++brace_depth;
        }
        if (token.kind == TokenKind::RightBrace && brace_depth != 0) {
            --brace_depth;
        }
    }
}

auto Parser::at_end() const noexcept -> bool {
    return cursor >= tokens.size();
}

auto Parser::current() const noexcept -> Token {
    if (cursor < tokens.size()) {
        if (split_right_shift) {
            return {
                .kind = TokenKind::Greater,
                .span =
                    Span::from_bounds(tokens[cursor].span.start() + 1, tokens[cursor].span.end())
            };
        }
        return tokens[cursor];
    }
    const auto end = static_cast<std::uint32_t>(source.size());
    return {
        .kind = TokenKind::Invalid,
        .span = Span::at(end),
    };
}

auto Parser::check(TokenKind kind) const noexcept -> bool {
    return !at_end() && current().kind == kind;
}

auto Parser::check_next(TokenKind kind) const noexcept -> bool {
    return cursor + 1 < tokens.size() && tokens[cursor + 1].kind == kind;
}

auto Parser::consume() noexcept -> Token {
    const auto token = current();
    split_right_shift = false;
    if (cursor < tokens.size()) {
        ++cursor;
    }
    return token;
}

auto Parser::match(TokenKind kind) noexcept -> std::optional<Token> {
    if (!check(kind)) {
        return std::nullopt;
    }
    return consume();
}

auto Parser::fail(std::string_view message, Span span, DiagnosticCode code) noexcept -> void {
    if (failed) {
        return;
    }
    failed = true;
    if (speculation_depth == 0) {
        diagnostics.push_back(
            DiagnosticBuilder(code, std::string(message)).primary(locate(source_id, span)).build()
        );
    } else {
        remember_speculative_failure({
            .span = span,
            .message = std::string(message),
            .code = code,
        });
    }
}

auto Parser::fail_here(std::string_view message) noexcept -> void {
    fail(message, current().span);
}

auto Parser::expect(TokenKind kind, std::string_view message) noexcept -> Token {
    if (check(kind)) {
        return consume();
    }
    fail_here(message);
    return current();
}

auto Parser::save() const noexcept -> Checkpoint {
    return {
        .cursor = cursor,
        .split_right_shift = split_right_shift,
        .builder = builder.checkpoint(),
        .failed = failed,
        .diagnostic_count = diagnostics.size(),
        .expression_nesting = expression_nesting,
        .block_boundary_depth = block_boundary_depth,
        .syntax_nesting = syntax_nesting,
    };
}

auto Parser::restore(const Checkpoint& checkpoint) noexcept -> void {
    builder.rewind(checkpoint.builder);
    cursor = checkpoint.cursor;
    split_right_shift = checkpoint.split_right_shift;
    failed = checkpoint.failed;
    diagnostics.resize(checkpoint.diagnostic_count);
    expression_nesting = checkpoint.expression_nesting;
    block_boundary_depth = checkpoint.block_boundary_depth;
    syntax_nesting = checkpoint.syntax_nesting;
}

auto Parser::enter_syntax_nesting() noexcept -> DepthGuard {
    if (syntax_nesting >= maximum_syntax_nesting) {
        fail(
            "syntax nesting exceeds the limit of 512",
            current().span,
            DiagnosticCode::ParseNestingTooDeep
        );
        return DepthGuard(nullptr);
    }
    return enter_depth(syntax_nesting);
}

auto Parser::preflight_delimiter_nesting() noexcept -> bool {
    struct OpenDelimiter final {
        TokenKind kind;
        Span span;
    };

    auto delimiters = std::vector<OpenDelimiter> {};
    delimiters.reserve(maximum_syntax_nesting);
    const auto spelling = [](TokenKind kind) static noexcept -> std::string_view {
        switch (kind) {
            case TokenKind::LeftParen:    return "(";
            case TokenKind::RightParen:   return ")";
            case TokenKind::LeftBracket:  return "[";
            case TokenKind::RightBracket: return "]";
            case TokenKind::LeftBrace:    return "{";
            case TokenKind::RightBrace:   return "}";
            default:                      return "";
        }
    };
    const auto closing = [](TokenKind kind) static noexcept -> TokenKind {
        switch (kind) {
            case TokenKind::InterpolationStart: return TokenKind::InterpolationEnd;
            case TokenKind::InterpolationOpen:  return TokenKind::InterpolationClose;
            case TokenKind::LeftParen:          return TokenKind::RightParen;
            case TokenKind::LeftBracket:        return TokenKind::RightBracket;
            case TokenKind::LeftBrace:          return TokenKind::RightBrace;
            default:                            return TokenKind::Invalid;
        }
    };

    for (const auto& token : tokens) {
        const auto opening = token.kind == TokenKind::LeftParen
            || token.kind == TokenKind::LeftBracket
            || token.kind == TokenKind::LeftBrace
            || token.kind == TokenKind::InterpolationStart
            || token.kind == TokenKind::InterpolationOpen;
        if (opening) {
            if (delimiters.size() == maximum_syntax_nesting) {
                fail(
                    "syntax nesting exceeds the limit of 512",
                    token.span,
                    DiagnosticCode::ParseNestingTooDeep
                );
                return false;
            }
            delimiters.push_back({.kind = token.kind, .span = token.span});
            continue;
        }

        const auto closing_token = token.kind == TokenKind::RightParen
            || token.kind == TokenKind::RightBracket
            || token.kind == TokenKind::RightBrace
            || token.kind == TokenKind::InterpolationEnd
            || token.kind == TokenKind::InterpolationClose;
        if (!closing_token) {
            continue;
        }
        if (delimiters.empty()) {
            fail(
                std::format("unexpected closing delimiter '{}'", spelling(token.kind)),
                token.span
            );
            return false;
        }
        const auto expected = closing(delimiters.back().kind);
        if (token.kind == expected) {
            delimiters.pop_back();
            continue;
        }
        fail(
            std::format(
                "mismatched closing delimiter '{}'; expected '{}'",
                spelling(token.kind),
                spelling(expected)
            ),
            token.span
        );
        return false;
    }
    if (!delimiters.empty()) {
        const auto& delimiter = delimiters.back();
        fail(
            std::format(
                "unclosed delimiter '{}'; expected '{}'",
                spelling(delimiter.kind),
                spelling(closing(delimiter.kind))
            ),
            delimiter.span
        );
        return false;
    }
    return true;
}

auto Parser::begin_speculation() noexcept -> Checkpoint {
    const auto checkpoint = save();
    ++speculation_depth;
    speculation_failures.emplace_back();
    return checkpoint;
}

auto Parser::finish_speculation(const Checkpoint& checkpoint, bool commit) noexcept -> void {
    --speculation_depth;
    auto failure = std::move(speculation_failures.back());
    speculation_failures.pop_back();
    if (!commit) {
        restore(checkpoint);
        if (failure.has_value()) {
            remember_speculative_failure(std::move(*failure));
        }
    } else {
        failed = checkpoint.failed;
        diagnostics.resize(checkpoint.diagnostic_count);
    }
}

auto Parser::remember_speculative_failure(ParseFailure failure) noexcept -> void {
    if (!speculation_failures.empty()) {
        auto& current = speculation_failures.back();
        if (!current.has_value() || failure.span.start() >= current->span.start()) {
            current = std::move(failure);
        }
        return;
    }
    if (!furthest_speculative_failure.has_value()
        || failure.span.start() >= furthest_speculative_failure->span.start()) {
        furthest_speculative_failure = std::move(failure);
    }
}

auto Parser::join(Span first, Span last) noexcept -> Span {
    return Span::from_bounds(first.start(), last.end());
}

auto Parser::make_cpp_source_fragment(Span full_span) const noexcept -> ASTCppSourceFragment {
    auto payload_start = full_span.start();
    while (payload_start < full_span.end()
           && source[payload_start] != '\n'
           && source[payload_start] != '\r') {
        ++payload_start;
    }
    if (payload_start < full_span.end() && source[payload_start] == '\r') {
        ++payload_start;
    }
    if (payload_start < full_span.end() && source[payload_start] == '\n') {
        ++payload_start;
    }
    auto payload_end = full_span.end();
    while (payload_end > payload_start
           && source[payload_end - 1] != '\n'
           && source[payload_end - 1] != '\r') {
        --payload_end;
    }
    return {
        .form_span = full_span,
        .payload_span = Span::from_bounds(payload_start, payload_end),
    };
}
